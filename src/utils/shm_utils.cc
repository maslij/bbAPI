#include "utils/shm_utils.h"
#include <random>
#include <iostream>
#include <tuple>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "utils/url_utils.h"

// Implementation of low-level shared memory operations
namespace triton
{
    namespace client
    {

        Error
        CreateSharedMemoryRegion(std::string shm_key, size_t byte_size, int *shm_fd)
        {
            // get shared memory region descriptor
            *shm_fd = shm_open(shm_key.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            if (*shm_fd == -1)
            {
                return Error(
                    "unable to get shared memory descriptor for shared-memory key '" +
                    shm_key + "'");
            }
            // extend shared memory object as by default it's initialized with size 0
            int res = ftruncate(*shm_fd, byte_size);
            if (res == -1)
            {
                return Error(
                    "unable to initialize shared-memory key '" + shm_key +
                    "' to requested size: " + std::to_string(byte_size) + " bytes");
            }

            return Error::Success;
        }

        Error
        MapSharedMemory(int shm_fd, size_t offset, size_t byte_size, void **shm_addr)
        {
            // map shared memory to process address space
            *shm_addr =
                mmap(NULL, byte_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, offset);
            if (*shm_addr == MAP_FAILED)
            {
                return Error(
                    "unable to process address space or shared-memory descriptor: " +
                    std::to_string(shm_fd));
            }

            return Error::Success;
        }

        Error
        CloseSharedMemory(int shm_fd)
        {
            // close shared memory descriptor
            if (close(shm_fd) == -1)
            {
                return Error(
                    "unable to close shared-memory descriptor: " + std::to_string(shm_fd));
            }

            return Error::Success;
        }

        Error
        UnlinkSharedMemoryRegion(std::string shm_key)
        {
            int shm_fd = shm_unlink(shm_key.c_str());
            if (shm_fd == -1)
            {
                return Error("unable to unlink shared memory for key '" + shm_key + "'");
            }

            return Error::Success;
        }

        Error
        UnmapSharedMemory(void *shm_addr, size_t byte_size)
        {
            int tmp_fd = munmap(shm_addr, byte_size);
            if (tmp_fd == -1)
            {
                return Error("unable to munmap shared memory region");
            }

            return Error::Success;
        }

    }
} // namespace triton::client

namespace tapi
{
    namespace utils
    {

        TritonSharedMemory::TritonSharedMemory()
            : fd_(-1), addr_(nullptr), size_(0), isValid_(false)
        {
        }

        TritonSharedMemory::~TritonSharedMemory()
        {
            cleanup();
        }

        std::string TritonSharedMemory::createImageSharedMemory(
            const cv::Mat &image, const std::string &name, bool skipRegistration)
        {

            // Clean up any previous shared memory
            cleanup();

            // Generate a unique name if one wasn't provided
            if (name.empty())
            {
                name_ = "tapi_img_" + generateRandomString();
            }
            else
            {
                name_ = name;
            }

            // Convert image to float32 format for model input
            // Calculate the size needed for the float32 image (4 bytes per pixel value)
            size_ = image.total() * image.channels() * sizeof(float);

            std::cout << "Creating shared memory with size: " << size_ << " bytes for image "
                      << image.cols << "x" << image.rows << "x" << image.channels() << std::endl;

            // Create shared memory region
            auto err = triton::client::CreateSharedMemoryRegion(name_, size_, &fd_);
            if (!err.IsOk())
            {
                std::cerr << "Failed to create shared memory region: " << err.Message() << std::endl;
                return "";
            }

            // Map the memory
            err = triton::client::MapSharedMemory(fd_, 0, size_, &addr_);
            if (!err.IsOk())
            {
                std::cerr << "Failed to map shared memory: " << err.Message() << std::endl;
                triton::client::CloseSharedMemory(fd_);
                fd_ = -1;
                return "";
            }

            // Convert and normalize the image to float32 (0-1 range)
            // This is usually what neural network models expect
            cv::Mat floatImage;
            image.convertTo(floatImage, CV_32FC3, 1.0f / 255.0f);

            // Copy float data to shared memory
            float *dst = static_cast<float *>(addr_);

            // Format the data for the specific model
            // Most ONNX models use NCHW format (channels-first)
            if (floatImage.isContinuous())
            {
                // For NCHW format (common in ONNX models)
                const int height = floatImage.rows;
                const int width = floatImage.cols;
                const int channels = floatImage.channels();

                // Rearrange from HWC to CHW format
                for (int c = 0; c < channels; ++c)
                {
                    for (int h = 0; h < height; ++h)
                    {
                        for (int w = 0; w < width; ++w)
                        {
                            const float val = floatImage.at<cv::Vec3f>(h, w)[c];
                            const size_t idx = c * (height * width) + h * width + w;
                            dst[idx] = val;
                        }
                    }
                }
            }
            else
            {
                std::cerr << "Warning: Image is not continuous, this may cause errors" << std::endl;
                // Fallback for non-continuous images (less efficient)
                size_t index = 0;
                const int height = floatImage.rows;
                const int width = floatImage.cols;
                const int channels = floatImage.channels();

                for (int c = 0; c < channels; ++c)
                {
                    for (int h = 0; h < height; ++h)
                    {
                        for (int w = 0; w < width; ++w)
                        {
                            dst[index++] = floatImage.at<cv::Vec3f>(h, w)[c];
                        }
                    }
                }
            }

            isValid_ = true;

            // Register the shared memory with Triton server if not skipped
            if (!skipRegistration && !registerWithTritonServer())
            {
                std::cerr << "Failed to register shared memory with Triton server" << std::endl;
                cleanup();
                return "";
            }

            return name_;
        }

        bool TritonSharedMemory::registerWithTritonServer()
        {
            // Get Triton server URL from environment or use default
            std::string serverUrl = getenv("TRITON_SERVER_URL") ? getenv("TRITON_SERVER_URL") : tapi::utils::getServerUrlFromEnvOrConfig();

            CURL *curl = curl_easy_init();
            if (!curl)
            {
                std::cerr << "Failed to initialize CURL for shared memory registration" << std::endl;
                return false;
            }

            bool success = false;

            try
            {
                // Prepare the registration endpoint URL
                std::string url = serverUrl;
                if (url.back() != '/')
                {
                    url += "/v2/systemsharedmemory/region/" + name_ + "/register";
                }
                else
                {
                    url += "v2/systemsharedmemory/region/" + name_ + "/register";
                }

                std::cout << "Registering shared memory with Triton at: " << url << std::endl;
                std::cout << "Shared memory size: " << size_ << " bytes" << std::endl;

                // Create the JSON payload
                nlohmann::json requestJson;
                requestJson["key"] = name_; // Use same name as key
                requestJson["offset"] = 0;
                requestJson["byte_size"] = size_;
                std::string requestBody = requestJson.dump();

                std::cout << "Registration payload: " << requestBody << std::endl;

                // Set up the request
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L); // 5 second timeout

                // Set headers
                struct curl_slist *headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

                // Prepare for response
                std::string response;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

                // Execute the request
                CURLcode res = curl_easy_perform(curl);

                // Clean up headers
                curl_slist_free_all(headers);

                if (res != CURLE_OK)
                {
                    std::cerr << "CURL error during shared memory registration: " << curl_easy_strerror(res) << std::endl;
                }
                else
                {
                    // Check the response code
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                    std::cout << "Triton server response code: " << http_code << std::endl;
                    if (!response.empty())
                    {
                        std::cout << "Response: " << response << std::endl;
                    }

                    if (http_code == 200)
                    {
                        std::cout << "Successfully registered shared memory region '" << name_ << "' with Triton server" << std::endl;
                        success = true;
                    }
                    else
                    {
                        std::cerr << "Server returned error code " << http_code << " during shared memory registration: " << response << std::endl;

                        // Try alternative registration endpoint (for older Triton versions)
                        url = serverUrl;
                        if (url.back() != '/')
                        {
                            url += "/v2/systemsharedmemory/register";
                        }
                        else
                        {
                            url += "v2/systemsharedmemory/register";
                        }

                        std::cout << "Trying alternative registration endpoint: " << url << std::endl;

                        // Set up alternative request
                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

                        // Create alternative payload format
                        nlohmann::json altRequestJson;
                        altRequestJson["name"] = name_;
                        altRequestJson["key"] = name_;
                        altRequestJson["offset"] = 0;
                        altRequestJson["byte_size"] = size_;
                        std::string altRequestBody = altRequestJson.dump();

                        std::cout << "Alternative payload: " << altRequestBody << std::endl;

                        // Update headers
                        headers = NULL;
                        headers = curl_slist_append(headers, "Content-Type: application/json");
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, altRequestBody.c_str());

                        // Clear previous response
                        response.clear();

                        // Execute request
                        res = curl_easy_perform(curl);

                        // Clean up
                        curl_slist_free_all(headers);

                        if (res != CURLE_OK)
                        {
                            std::cerr << "CURL error during alternative registration: " << curl_easy_strerror(res) << std::endl;
                        }
                        else
                        {
                            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                            std::cout << "Alternative registration response code: " << http_code << std::endl;
                            if (!response.empty())
                            {
                                std::cout << "Response: " << response << std::endl;
                            }

                            if (http_code == 200)
                            {
                                std::cout << "Successfully registered with alternative endpoint" << std::endl;
                                success = true;
                            }
                            else
                            {
                                std::cerr << "Alternative registration failed: " << response << std::endl;
                            }
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception during shared memory registration: " << e.what() << std::endl;
            }

            // Clean up CURL
            curl_easy_cleanup(curl);

            return success;
        }

        std::tuple<std::string, void *, size_t> TritonSharedMemory::getSharedMemoryInfo() const
        {
            return std::make_tuple(name_, addr_, size_);
        }

        void TritonSharedMemory::cleanup()
        {
            // Only unregister from server if we haven't already done so explicitly
            // This prevents double unregistration and potential errors
            static thread_local bool already_unregistered = false;
            if (isValid_ && !name_.empty() && !already_unregistered)
            {
                unregisterFromTritonServer();
                already_unregistered = true;
            }

            if (addr_ != nullptr)
            {
                triton::client::UnmapSharedMemory(addr_, size_);
                addr_ = nullptr;
            }

            if (fd_ >= 0)
            {
                triton::client::CloseSharedMemory(fd_);
                fd_ = -1;
            }

            if (!name_.empty() && isValid_)
            {
                triton::client::UnlinkSharedMemoryRegion(name_);
                name_.clear();
            }

            size_ = 0;
            isValid_ = false;
            already_unregistered = false; // Reset for next cleanup
        }

        bool TritonSharedMemory::unregisterFromTritonServer()
        {
            if (name_.empty())
            {
                return true; // Nothing to unregister
            }

            // Get Triton server URL from environment or use default
            std::string serverUrl = getenv("TRITON_SERVER_URL") ? getenv("TRITON_SERVER_URL") : tapi::utils::getServerUrlFromEnvOrConfig();

            CURL *curl = curl_easy_init();
            if (!curl)
            {
                std::cerr << "Failed to initialize CURL for shared memory unregistration" << std::endl;
                return false;
            }

            bool success = false;

            try
            {
                // Prepare the unregistration endpoint URL
                std::string url = serverUrl;
                if (url.back() != '/')
                {
                    url += "/v2/systemsharedmemory/region/" + name_ + "/unregister";
                }
                else
                {
                    url += "v2/systemsharedmemory/region/" + name_ + "/unregister";
                }

                std::cout << "Unregistering shared memory from Triton at: " << url << std::endl;

                // Set up the request
                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""); // Empty POST body

                // Prepare for response
                std::string response;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

                // Execute the request
                CURLcode res = curl_easy_perform(curl);

                if (res != CURLE_OK)
                {
                    std::cerr << "CURL error during shared memory unregistration: " << curl_easy_strerror(res) << std::endl;
                }
                else
                {
                    // Check the response code
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                    if (http_code == 200)
                    {
                        std::cout << "Successfully unregistered shared memory region '" << name_ << "' from Triton server" << std::endl;
                        success = true;
                    }
                    else
                    {
                        std::cerr << "Server returned error code " << http_code << " during shared memory unregistration: " << response << std::endl;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception during shared memory unregistration: " << e.what() << std::endl;
            }

            // Clean up CURL
            curl_easy_cleanup(curl);

            return success;
        }

        bool TritonSharedMemory::isValid() const
        {
            return isValid_ && (addr_ != nullptr) && (fd_ >= 0) && !name_.empty();
        }

        std::string TritonSharedMemory::generateRandomString(size_t length)
        {
            static const char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> dist(0, sizeof(alphanum) - 2);

            std::string result;
            result.reserve(length);
            for (size_t i = 0; i < length; ++i)
            {
                result += alphanum[dist(gen)];
            }

            return result;
        }

    } // namespace utils
} // namespace tapi