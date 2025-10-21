#pragma once

#include <string>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <libpq-fe.h>
#include <nlohmann/json.hpp>

namespace tapi {
namespace database {

/**
 * @brief PostgreSQL connection wrapper with automatic reconnection
 */
class PostgreSQLConnection {
public:
    PostgreSQLConnection(const std::string& conninfo);
    ~PostgreSQLConnection();
    
    // Disable copy
    PostgreSQLConnection(const PostgreSQLConnection&) = delete;
    PostgreSQLConnection& operator=(const PostgreSQLConnection&) = delete;
    
    // Get the underlying libpq connection (for advanced usage)
    PGconn* getRawConnection() { return conn_; }
    
    // Connection status
    bool isConnected() const;
    bool reconnect();
    
    // Execute a query with no results (INSERT, UPDATE, DELETE)
    bool execute(const std::string& query);
    
    // Execute a query with results
    PGresult* executeQuery(const std::string& query);
    
    // Execute a parameterized query
    PGresult* executeParams(const std::string& query,
                           const std::vector<std::string>& params);
    
    // Clear result (free memory)
    static void clearResult(PGresult* result);
    
    // Get error message
    std::string getLastError() const;
    
    // Transaction support
    bool beginTransaction();
    bool commit();
    bool rollback();
    
private:
    PGconn* conn_;
    std::string conninfo_;
    mutable std::mutex conn_mutex_;
    
    bool connect();
    void handleConnectionError();
};

/**
 * @brief PostgreSQL connection pool for thread-safe access
 */
class PostgreSQLConnectionPool {
public:
    struct Config {
        std::string host = "localhost";
        int port = 5432;
        std::string database = "tapi_edge";
        std::string user = "tapi_user";
        std::string password = "";
        int pool_size = 10;
        int connection_timeout_ms = 5000;
        int max_retries = 3;
        
        std::string toConnectionString() const;
    };
    
    /**
     * @brief RAII connection wrapper for automatic return to pool
     */
    class ConnectionGuard {
    public:
        ConnectionGuard(PostgreSQLConnectionPool* pool, std::shared_ptr<PostgreSQLConnection> conn)
            : pool_(pool), conn_(conn) {}
        
        ~ConnectionGuard() {
            if (conn_ && pool_) {
                pool_->returnConnection(conn_);
            }
        }
        
        // Disable copy
        ConnectionGuard(const ConnectionGuard&) = delete;
        ConnectionGuard& operator=(const ConnectionGuard&) = delete;
        
        // Allow move
        ConnectionGuard(ConnectionGuard&& other) noexcept
            : pool_(other.pool_), conn_(std::move(other.conn_)) {
            other.pool_ = nullptr;
        }
        
        PostgreSQLConnection* operator->() { return conn_.get(); }
        PostgreSQLConnection& operator*() { return *conn_; }
        const PostgreSQLConnection* operator->() const { return conn_.get(); }
        const PostgreSQLConnection& operator*() const { return *conn_; }
        
        bool isValid() const { return conn_ && conn_->isConnected(); }
        
    private:
        PostgreSQLConnectionPool* pool_;
        std::shared_ptr<PostgreSQLConnection> conn_;
    };
    
    PostgreSQLConnectionPool(const Config& config);
    ~PostgreSQLConnectionPool();
    
    // Disable copy
    PostgreSQLConnectionPool(const PostgreSQLConnectionPool&) = delete;
    PostgreSQLConnectionPool& operator=(const PostgreSQLConnectionPool&) = delete;
    
    // Get a connection from the pool (blocks if all connections are in use)
    ConnectionGuard getConnection();
    
    // Get a connection with timeout (returns nullptr if timeout)
    ConnectionGuard getConnection(int timeout_ms);
    
    // Check pool health
    bool isHealthy() const;
    int getAvailableConnections() const;
    int getTotalConnections() const;
    
    // Execute a query with automatic connection management
    bool executeQuick(const std::string& query);
    PGresult* queryQuick(const std::string& query);
    
    // Transaction helper
    template<typename Func>
    bool executeTransaction(Func&& func);
    
private:
    Config config_;
    std::queue<std::shared_ptr<PostgreSQLConnection>> available_connections_;
    std::vector<std::shared_ptr<PostgreSQLConnection>> all_connections_;
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_cv_;
    bool shutdown_;
    
    void initializePool();
    void returnConnection(std::shared_ptr<PostgreSQLConnection> conn);
    
    friend class ConnectionGuard;
};

/**
 * @brief Helper class for prepared statements with automatic parameter binding
 */
class PreparedStatement {
public:
    PreparedStatement(PostgreSQLConnection* conn, const std::string& stmt_name, const std::string& query);
    
    // Bind parameters
    PreparedStatement& bind(const std::string& value);
    PreparedStatement& bind(int value);
    PreparedStatement& bind(long value);
    PreparedStatement& bind(double value);
    PreparedStatement& bind(bool value);
    PreparedStatement& bindNull();
    PreparedStatement& bindJson(const nlohmann::json& value);
    
    // Execute the statement
    PGresult* execute();
    
    // Reset for reuse
    void reset();
    
private:
    PostgreSQLConnection* conn_;
    std::string stmt_name_;
    std::string query_;
    std::vector<std::string> params_;
    bool prepared_;
    
    void prepare();
};

/**
 * @brief Result set wrapper for easier data access
 */
class ResultSet {
public:
    ResultSet(PGresult* result);
    ~ResultSet();
    
    // Disable copy
    ResultSet(const ResultSet&) = delete;
    ResultSet& operator=(const ResultSet&) = delete;
    
    // Allow move
    ResultSet(ResultSet&& other) noexcept;
    ResultSet& operator=(ResultSet&& other) noexcept;
    
    // Check if query was successful
    bool isValid() const;
    
    // Row and column info
    int getRowCount() const;
    int getColumnCount() const;
    std::string getColumnName(int col) const;
    
    // Data access
    bool isNull(int row, int col) const;
    std::string getString(int row, int col) const;
    int getInt(int row, int col) const;
    long getLong(int row, int col) const;
    double getDouble(int row, int col) const;
    bool getBool(int row, int col) const;
    nlohmann::json getJson(int row, int col) const;
    
    // Iterator support for easier row access
    class Row {
    public:
        Row(const ResultSet* rs, int row) : rs_(rs), row_(row) {}
        
        std::string getString(int col) const { return rs_->getString(row_, col); }
        std::string getString(const std::string& colName) const;
        int getInt(int col) const { return rs_->getInt(row_, col); }
        int getInt(const std::string& colName) const;
        long getLong(int col) const { return rs_->getLong(row_, col); }
        double getDouble(int col) const { return rs_->getDouble(row_, col); }
        bool getBool(int col) const { return rs_->getBool(row_, col); }
        nlohmann::json getJson(int col) const { return rs_->getJson(row_, col); }
        bool isNull(int col) const { return rs_->isNull(row_, col); }
        
    private:
        const ResultSet* rs_;
        int row_;
    };
    
    Row getRow(int row) const { return Row(this, row); }
    
private:
    PGresult* result_;
    std::map<std::string, int> column_map_;  // Cache column name to index mapping
    
    void buildColumnMap();
    int getColumnIndex(const std::string& colName) const;
};

// Template implementation
template<typename Func>
bool PostgreSQLConnectionPool::executeTransaction(Func&& func) {
    auto guard = getConnection();
    if (!guard.isValid()) {
        return false;
    }
    
    if (!guard->beginTransaction()) {
        return false;
    }
    
    try {
        bool success = func(*guard);
        if (success) {
            return guard->commit();
        } else {
            guard->rollback();
            return false;
        }
    } catch (...) {
        guard->rollback();
        throw;
    }
}

} // namespace database
} // namespace tapi

