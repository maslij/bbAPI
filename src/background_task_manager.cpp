#include "api.h"
#include "logger.h"
#include <uuid/uuid.h>
#include <chrono>
#include <thread>

namespace tapi {

BackgroundTaskManager::BackgroundTaskManager() : running_(true) {
    // Start the worker thread
    workerThread_ = std::thread(&BackgroundTaskManager::workerThread, this);
    LOG_INFO("BackgroundTaskManager", "Background task manager started");
}

void BackgroundTaskManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return; // Already shut down
        }
        running_ = false;
    }
    
    cv_.notify_all();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    LOG_INFO("BackgroundTaskManager", "Background task manager shut down");
}

std::string BackgroundTaskManager::submitTask(
    std::string taskType, 
    std::string targetId, 
    std::function<bool(std::function<void(double, std::string)>)> taskFunc) {
    
    // Generate a task ID
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_str);
    std::string taskId = std::string(uuid_str);
    
    // Create task status
    TaskStatus status;
    status.state = TaskStatus::State::PENDING;
    status.taskId = taskId;
    status.taskType = taskType;
    status.targetId = targetId;
    status.progress = 0.0;
    status.message = "Task pending";
    status.createdAt = std::chrono::system_clock::now();
    status.updatedAt = status.createdAt;
    
    // Add task to queue
    {
        std::lock_guard<std::mutex> lock(mutex_);
        taskStatuses_[taskId] = status;
        
        Task task;
        task.id = taskId;
        task.type = taskType;
        task.targetId = targetId;
        task.func = taskFunc;
        task.createdAt = status.createdAt;
        
        taskQueue_.push(task);
    }
    
    // Notify worker thread
    cv_.notify_one();
    
    LOG_INFO("BackgroundTaskManager", "Task submitted: " + taskId + " [" + taskType + "] for " + targetId);
    
    return taskId;
}

BackgroundTaskManager::TaskStatus BackgroundTaskManager::getTaskStatus(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = taskStatuses_.find(taskId);
    if (it != taskStatuses_.end()) {
        return it->second;
    }
    
    // Return an empty status if not found
    TaskStatus emptyStatus;
    emptyStatus.state = TaskStatus::State::FAILED;
    emptyStatus.taskId = taskId;
    emptyStatus.message = "Task not found";
    return emptyStatus;
}

std::vector<BackgroundTaskManager::TaskStatus> BackgroundTaskManager::getAllTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TaskStatus> tasks;
    for (const auto& pair : taskStatuses_) {
        tasks.push_back(pair.second);
    }
    
    return tasks;
}

void BackgroundTaskManager::cleanupOldTasks(int maxAgeSecs) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> tasksToRemove;
    
    for (const auto& pair : taskStatuses_) {
        const auto& status = pair.second;
        
        // Only clean up completed or failed tasks
        if (status.state == TaskStatus::State::COMPLETED || 
            status.state == TaskStatus::State::FAILED) {
            
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - status.updatedAt).count();
                
            if (age > maxAgeSecs) {
                tasksToRemove.push_back(status.taskId);
            }
        }
    }
    
    for (const auto& taskId : tasksToRemove) {
        taskStatuses_.erase(taskId);
    }
    
    if (!tasksToRemove.empty()) {
        LOG_INFO("BackgroundTaskManager", "Cleaned up " + std::to_string(tasksToRemove.size()) + " old tasks");
    }
}

void BackgroundTaskManager::workerThread() {
    while (running_) {
        Task currentTask;
        
        // Wait for a task to be available
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { 
                return !running_ || !taskQueue_.empty(); 
            });
            
            if (!running_) {
                break;
            }
            
            if (!taskQueue_.empty()) {
                currentTask = taskQueue_.front();
                taskQueue_.pop();
                
                // Update status to running
                auto& status = taskStatuses_[currentTask.id];
                status.state = TaskStatus::State::RUNNING;
                status.message = "Task running";
                status.updatedAt = std::chrono::system_clock::now();
            }
        }
        
        if (currentTask.id.empty()) {
            continue;
        }
        
        LOG_INFO("BackgroundTaskManager", "Starting task: " + currentTask.id);
        
        // Execute the task
        bool success = false;
        try {
            // Create progress callback
            auto progressCallback = [this, &currentTask](double progress, std::string message) {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = taskStatuses_.find(currentTask.id);
                if (it != taskStatuses_.end()) {
                    it->second.progress = progress;
                    it->second.message = message;
                    it->second.updatedAt = std::chrono::system_clock::now();
                }
            };
            
            // Run the task function with progress callback
            success = currentTask.func(progressCallback);
            
            // Update status based on result
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = taskStatuses_.find(currentTask.id);
                if (it != taskStatuses_.end()) {
                    if (success) {
                        it->second.state = TaskStatus::State::COMPLETED;
                        it->second.progress = 100.0;
                        it->second.message = "Task completed successfully";
                    } else {
                        it->second.state = TaskStatus::State::FAILED;
                        it->second.message = "Task failed";
                    }
                    it->second.updatedAt = std::chrono::system_clock::now();
                }
            }
            
            LOG_INFO("BackgroundTaskManager", "Task " + currentTask.id + " " + 
                    (success ? "completed successfully" : "failed"));
        }
        catch (const std::exception& e) {
            // Handle exceptions from task
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = taskStatuses_.find(currentTask.id);
            if (it != taskStatuses_.end()) {
                it->second.state = TaskStatus::State::FAILED;
                it->second.message = "Task failed with exception: " + std::string(e.what());
                it->second.updatedAt = std::chrono::system_clock::now();
            }
            
            LOG_ERROR("BackgroundTaskManager", "Task " + currentTask.id + 
                     " failed with exception: " + e.what());
        }
        catch (...) {
            // Handle unknown exceptions
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = taskStatuses_.find(currentTask.id);
            if (it != taskStatuses_.end()) {
                it->second.state = TaskStatus::State::FAILED;
                it->second.message = "Task failed with unknown exception";
                it->second.updatedAt = std::chrono::system_clock::now();
            }
            
            LOG_ERROR("BackgroundTaskManager", "Task " + currentTask.id + 
                     " failed with unknown exception");
        }
    }
}

} // namespace tapi 