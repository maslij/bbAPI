#pragma once

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "utils/common.h"

// Low-level shared memory operations in the triton::client namespace
namespace triton { namespace client {

// Create a shared memory region of the size 'byte_size' and return the unique
// identifier.
// \param shm_key The string identifier of the shared memory region
// \param byte_size The size in bytes of the shared memory region
// \param shm_fd Returns an int descriptor of the created shared memory region
// \return error Returns an error if unable to open shared memory region.
Error CreateSharedMemoryRegion(
    std::string shm_key, size_t byte_size, int* shm_fd);

// Mmap the shared memory region with the given 'offset' and 'byte_size' and
// return the base address of the region.
// \param shm_fd The int descriptor of the created shared memory region
// \param offset The offset of the shared memory block from the start of the
// shared memory region
// \param byte_size The size in bytes of the shared memory region
// \param shm_addr Returns the base address of the shared memory region
// \return error Returns an error if unable to mmap shared memory region.
Error MapSharedMemory(
    int shm_fd, size_t offset, size_t byte_size, void** shm_addr);

// Close the shared memory descriptor.
// \param shm_fd The int descriptor of the created shared memory region
// \return error Returns an error if unable to close shared memory descriptor.
Error CloseSharedMemory(int shm_fd);

// Destroy the shared memory region with the given name.
// \return error Returns an error if unable to unlink shared memory region.
Error UnlinkSharedMemoryRegion(std::string shm_key);

// Munmap the shared memory region from the base address with the given
// byte_size.
// \return error Returns an error if unable to unmap shared memory region.
Error UnmapSharedMemory(void* shm_addr, size_t byte_size);

}}  // namespace triton::client

namespace tapi {
namespace utils {

/**
 * @brief Manages a shared memory region for use with Triton Inference Server
 */
class TritonSharedMemory {
public:
    /**
     * @brief Construct a new Triton Shared Memory object
     */
    TritonSharedMemory();
    
    /**
     * @brief Destroy the Triton Shared Memory object
     */
    ~TritonSharedMemory();
    
    /**
     * @brief Create shared memory for an OpenCV image
     * 
     * @param image The image to copy to shared memory
     * @param name Optional specific name to use (generates random name if empty)
     * @param skipRegistration Optional parameter to skip Triton server registration
     * @return std::string The name of the shared memory region
     */
    std::string createImageSharedMemory(const cv::Mat& image, const std::string& name = "", bool skipRegistration = false);
    
    /**
     * @brief Get information about the current shared memory
     * 
     * @return std::tuple<std::string, void*, size_t> Name, pointer, and size of shared memory
     */
    std::tuple<std::string, void*, size_t> getSharedMemoryInfo() const;
    
    /**
     * @brief Clean up shared memory resources
     */
    void cleanup();
    
    /**
     * @brief Check if shared memory has been initialized
     * 
     * @return true If shared memory is valid
     * @return false If shared memory is not initialized
     */
    bool isValid() const;
    
    /**
     * @brief Generate a random string for shared memory naming
     * 
     * @param length Length of the random string
     * @return std::string Random alphanumeric string
     */
    static std::string generateRandomString(size_t length = 8);
    
    /**
     * @brief Register the shared memory with Triton server
     * 
     * @return true If registration was successful
     * @return false If registration failed
     */
    bool registerWithTritonServer();
    
    /**
     * @brief Unregister the shared memory from Triton server
     * 
     * @return true If unregistration was successful
     * @return false If unregistration failed
     */
    bool unregisterFromTritonServer();

private:
    std::string name_;
    int fd_;
    void* addr_;
    size_t size_;
    bool isValid_;
};

} // namespace utils
} // namespace tapi 