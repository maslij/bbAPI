#include "database/postgres_connection.h"
#include "logger.h"
#include <sstream>
#include <thread>
#include <chrono>

namespace tapi {
namespace database {

// =====================================================
// PostgreSQLConnection Implementation
// =====================================================

PostgreSQLConnection::PostgreSQLConnection(const std::string& conninfo)
    : conn_(nullptr), conninfo_(conninfo) {
    connect();
}

PostgreSQLConnection::~PostgreSQLConnection() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool PostgreSQLConnection::connect() {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
    
    conn_ = PQconnectdb(conninfo_.c_str());
    
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        handleConnectionError();
        return false;
    }
    
    LOG_INFO("PostgreSQL", "Connected successfully");
    return true;
}

bool PostgreSQLConnection::isConnected() const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

bool PostgreSQLConnection::reconnect() {
    LOG_INFO("PostgreSQL", "Attempting to reconnect...");
    return connect();
}

bool PostgreSQLConnection::execute(const std::string& query) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        LOG_ERROR("PostgreSQL", "Not connected");
        return false;
    }
    
    PGresult* res = PQexec(conn_, query.c_str());
    ExecStatusType status = PQresultStatus(res);
    
    bool success = (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK);
    
    if (!success) {
        LOG_ERROR("PostgreSQL", "Query failed: " + std::string(PQerrorMessage(conn_)));
    }
    
    PQclear(res);
    return success;
}

PGresult* PostgreSQLConnection::executeQuery(const std::string& query) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        LOG_ERROR("PostgreSQL", "Not connected");
        return nullptr;
    }
    
    PGresult* res = PQexec(conn_, query.c_str());
    ExecStatusType status = PQresultStatus(res);
    
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        LOG_ERROR("PostgreSQL", "Query failed: " + std::string(PQerrorMessage(conn_)));
        PQclear(res);
        return nullptr;
    }
    
    return res;
}

PGresult* PostgreSQLConnection::executeParams(const std::string& query,
                                               const std::vector<std::string>& params) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        LOG_ERROR("PostgreSQL", "Not connected");
        return nullptr;
    }
    
    // Prepare parameter arrays for libpq
    std::vector<const char*> param_values;
    for (const auto& param : params) {
        param_values.push_back(param.c_str());
    }
    
    PGresult* res = PQexecParams(
        conn_,
        query.c_str(),
        params.size(),
        nullptr,  // Let server determine param types
        param_values.data(),
        nullptr,  // Text format for all params
        nullptr,  // Text format for all params
        0         // Request result in text format
    );
    
    ExecStatusType status = PQresultStatus(res);
    
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        LOG_ERROR("PostgreSQL", "Parameterized query failed: " + std::string(PQerrorMessage(conn_)));
        PQclear(res);
        return nullptr;
    }
    
    return res;
}

void PostgreSQLConnection::clearResult(PGresult* result) {
    if (result) {
        PQclear(result);
    }
}

std::string PostgreSQLConnection::getLastError() const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    if (conn_) {
        return std::string(PQerrorMessage(conn_));
    }
    return "Not connected";
}

bool PostgreSQLConnection::beginTransaction() {
    return execute("BEGIN");
}

bool PostgreSQLConnection::commit() {
    return execute("COMMIT");
}

bool PostgreSQLConnection::rollback() {
    return execute("ROLLBACK");
}

void PostgreSQLConnection::handleConnectionError() {
    if (conn_) {
        LOG_ERROR("PostgreSQL", "Connection failed: " + std::string(PQerrorMessage(conn_)));
    } else {
        LOG_ERROR("PostgreSQL", "Connection failed: unable to allocate connection");
    }
}

// =====================================================
// PostgreSQLConnectionPool Implementation
// =====================================================

std::string PostgreSQLConnectionPool::Config::toConnectionString() const {
    std::ostringstream oss;
    oss << "host=" << host
        << " port=" << port
        << " dbname=" << database
        << " user=" << user
        << " password=" << password
        << " connect_timeout=" << (connection_timeout_ms / 1000);
    return oss.str();
}

PostgreSQLConnectionPool::PostgreSQLConnectionPool(const Config& config)
    : config_(config), shutdown_(false) {
    initializePool();
}

PostgreSQLConnectionPool::~PostgreSQLConnectionPool() {
    shutdown_ = true;
    pool_cv_.notify_all();
    
    std::lock_guard<std::mutex> lock(pool_mutex_);
    all_connections_.clear();
    while (!available_connections_.empty()) {
        available_connections_.pop();
    }
}

void PostgreSQLConnectionPool::initializePool() {
    std::string connstr = config_.toConnectionString();
    
    LOG_INFO("PostgreSQLPool", "Initializing connection pool with " + 
             std::to_string(config_.pool_size) + " connections");
    
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    for (int i = 0; i < config_.pool_size; ++i) {
        auto conn = std::make_shared<PostgreSQLConnection>(connstr);
        if (conn->isConnected()) {
            all_connections_.push_back(conn);
            available_connections_.push(conn);
            LOG_DEBUG("PostgreSQLPool", "Connection " + std::to_string(i + 1) + " created");
        } else {
            LOG_ERROR("PostgreSQLPool", "Failed to create connection " + std::to_string(i + 1));
        }
    }
    
    LOG_INFO("PostgreSQLPool", "Pool initialized with " + 
             std::to_string(available_connections_.size()) + "/" + 
             std::to_string(config_.pool_size) + " connections");
}

PostgreSQLConnectionPool::ConnectionGuard PostgreSQLConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // Wait for available connection
    pool_cv_.wait(lock, [this] { 
        return !available_connections_.empty() || shutdown_; 
    });
    
    if (shutdown_) {
        return ConnectionGuard(this, nullptr);
    }
    
    auto conn = available_connections_.front();
    available_connections_.pop();
    
    // Verify connection is still valid
    if (!conn->isConnected()) {
        LOG_WARN("PostgreSQLPool", "Connection was stale, reconnecting...");
        if (!conn->reconnect()) {
            // Return to pool and try again
            available_connections_.push(conn);
            lock.unlock();
            pool_cv_.notify_one();
            
            // Retry
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return getConnection();
        }
    }
    
    return ConnectionGuard(this, conn);
}

PostgreSQLConnectionPool::ConnectionGuard PostgreSQLConnectionPool::getConnection(int timeout_ms) {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // Wait for available connection with timeout
    bool success = pool_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { 
        return !available_connections_.empty() || shutdown_; 
    });
    
    if (!success || shutdown_ || available_connections_.empty()) {
        LOG_WARN("PostgreSQLPool", "Timeout waiting for connection");
        return ConnectionGuard(this, nullptr);
    }
    
    auto conn = available_connections_.front();
    available_connections_.pop();
    
    // Verify connection
    if (!conn->isConnected()) {
        conn->reconnect();
    }
    
    return ConnectionGuard(this, conn);
}

void PostgreSQLConnectionPool::returnConnection(std::shared_ptr<PostgreSQLConnection> conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(pool_mutex_);
    available_connections_.push(conn);
    pool_cv_.notify_one();
}

bool PostgreSQLConnectionPool::isHealthy() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return !all_connections_.empty() && !shutdown_;
}

int PostgreSQLConnectionPool::getAvailableConnections() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return available_connections_.size();
}

int PostgreSQLConnectionPool::getTotalConnections() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return all_connections_.size();
}

bool PostgreSQLConnectionPool::executeQuick(const std::string& query) {
    auto guard = getConnection();
    if (!guard.isValid()) return false;
    return guard->execute(query);
}

PGresult* PostgreSQLConnectionPool::queryQuick(const std::string& query) {
    auto guard = getConnection();
    if (!guard.isValid()) return nullptr;
    return guard->executeQuery(query);
}

// =====================================================
// PreparedStatement Implementation
// =====================================================

PreparedStatement::PreparedStatement(PostgreSQLConnection* conn, 
                                    const std::string& stmt_name,
                                    const std::string& query)
    : conn_(conn), stmt_name_(stmt_name), query_(query), prepared_(false) {
}

PreparedStatement& PreparedStatement::bind(const std::string& value) {
    params_.push_back(value);
    return *this;
}

PreparedStatement& PreparedStatement::bind(int value) {
    params_.push_back(std::to_string(value));
    return *this;
}

PreparedStatement& PreparedStatement::bind(long value) {
    params_.push_back(std::to_string(value));
    return *this;
}

PreparedStatement& PreparedStatement::bind(double value) {
    params_.push_back(std::to_string(value));
    return *this;
}

PreparedStatement& PreparedStatement::bind(bool value) {
    params_.push_back(value ? "true" : "false");
    return *this;
}

PreparedStatement& PreparedStatement::bindNull() {
    params_.push_back("");  // libpq treats empty string as NULL if properly configured
    return *this;
}

PreparedStatement& PreparedStatement::bindJson(const nlohmann::json& value) {
    params_.push_back(value.dump());
    return *this;
}

PGresult* PreparedStatement::execute() {
    if (!conn_) {
        LOG_ERROR("PreparedStatement", "No connection available");
        return nullptr;
    }
    
    return conn_->executeParams(query_, params_);
}

void PreparedStatement::reset() {
    params_.clear();
}

void PreparedStatement::prepare() {
    // Preparation happens implicitly in PostgreSQL when using PQexecParams
    prepared_ = true;
}

// =====================================================
// ResultSet Implementation
// =====================================================

ResultSet::ResultSet(PGresult* result) : result_(result) {
    if (result_) {
        buildColumnMap();
    }
}

ResultSet::~ResultSet() {
    if (result_) {
        PQclear(result_);
        result_ = nullptr;
    }
}

ResultSet::ResultSet(ResultSet&& other) noexcept
    : result_(other.result_), column_map_(std::move(other.column_map_)) {
    other.result_ = nullptr;
}

ResultSet& ResultSet::operator=(ResultSet&& other) noexcept {
    if (this != &other) {
        if (result_) {
            PQclear(result_);
        }
        result_ = other.result_;
        column_map_ = std::move(other.column_map_);
        other.result_ = nullptr;
    }
    return *this;
}

bool ResultSet::isValid() const {
    return result_ != nullptr && 
           (PQresultStatus(result_) == PGRES_TUPLES_OK || 
            PQresultStatus(result_) == PGRES_COMMAND_OK);
}

int ResultSet::getRowCount() const {
    return result_ ? PQntuples(result_) : 0;
}

int ResultSet::getColumnCount() const {
    return result_ ? PQnfields(result_) : 0;
}

std::string ResultSet::getColumnName(int col) const {
    if (!result_ || col < 0 || col >= getColumnCount()) {
        return "";
    }
    return PQfname(result_, col);
}

bool ResultSet::isNull(int row, int col) const {
    if (!result_ || row < 0 || row >= getRowCount() || col < 0 || col >= getColumnCount()) {
        return true;
    }
    return PQgetisnull(result_, row, col);
}

std::string ResultSet::getString(int row, int col) const {
    if (isNull(row, col)) {
        return "";
    }
    return PQgetvalue(result_, row, col);
}

int ResultSet::getInt(int row, int col) const {
    if (isNull(row, col)) {
        return 0;
    }
    try {
        return std::stoi(PQgetvalue(result_, row, col));
    } catch (...) {
        return 0;
    }
}

long ResultSet::getLong(int row, int col) const {
    if (isNull(row, col)) {
        return 0;
    }
    try {
        return std::stol(PQgetvalue(result_, row, col));
    } catch (...) {
        return 0;
    }
}

double ResultSet::getDouble(int row, int col) const {
    if (isNull(row, col)) {
        return 0.0;
    }
    try {
        return std::stod(PQgetvalue(result_, row, col));
    } catch (...) {
        return 0.0;
    }
}

bool ResultSet::getBool(int row, int col) const {
    if (isNull(row, col)) {
        return false;
    }
    std::string val = PQgetvalue(result_, row, col);
    return (val == "t" || val == "true" || val == "1");
}

nlohmann::json ResultSet::getJson(int row, int col) const {
    if (isNull(row, col)) {
        return nlohmann::json();
    }
    try {
        return nlohmann::json::parse(PQgetvalue(result_, row, col));
    } catch (...) {
        LOG_ERROR("ResultSet", "Failed to parse JSON");
        return nlohmann::json();
    }
}

void ResultSet::buildColumnMap() {
    if (!result_) return;
    
    int col_count = getColumnCount();
    for (int i = 0; i < col_count; ++i) {
        std::string col_name = PQfname(result_, i);
        column_map_[col_name] = i;
    }
}

int ResultSet::getColumnIndex(const std::string& colName) const {
    auto it = column_map_.find(colName);
    if (it != column_map_.end()) {
        return it->second;
    }
    return -1;
}

std::string ResultSet::Row::getString(const std::string& colName) const {
    int col = rs_->getColumnIndex(colName);
    if (col < 0) {
        LOG_ERROR("ResultSet", "Column not found: " + colName);
        return "";
    }
    return rs_->getString(row_, col);
}

int ResultSet::Row::getInt(const std::string& colName) const {
    int col = rs_->getColumnIndex(colName);
    if (col < 0) {
        LOG_ERROR("ResultSet", "Column not found: " + colName);
        return 0;
    }
    return rs_->getInt(row_, col);
}

} // namespace database
} // namespace tapi

