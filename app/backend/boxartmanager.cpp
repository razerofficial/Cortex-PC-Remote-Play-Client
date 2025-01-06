#include "boxartmanager.h"
#include "../path.h"
#include "utils.h"

#include <fstream>
#include <boost/asio.hpp>
#include <glog/logging.h>

class ThreadPool 
{
public:
    ThreadPool(size_t numThreads)
        : workGuard(boost::asio::make_work_guard(ioContext))  // Ensure that io_context does not exit unexpectedly
    {
        // Create and start a thread
        for (size_t i = 0; i < numThreads; ++i) 
        {
            workers.emplace_back([this] {
                try 
                {
                    ioContext.run();  // 每个线程调用 io_context.run 来处理任务
                }
                catch (const std::exception& e) 
                {
                    LOG(ERROR) << "Exception caught: " << e.what() << std::endl;
                }
                });
        }
    }
    ~ThreadPool()
    {
        ioContext.stop();  // stop io_context
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();  // Wait for all threads to complete
            }
        }
    }

    // Adding tasks to the thread pool
    template<class F, class... Args>
    void enqueue(F&& f, Args&&... args)
    {
        // Submit tasks to the queue via io_context.post
        try {
            ioContext.post(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        }
        catch (const std::exception& e) {
            LOG(ERROR) << "Failed to enqueue task: " << e.what() << std::endl;
        }
    }

    // Wait for all tasks to complete
    void wait()
    {
        ioContext.stop();  // Stop io_context, all waiting tasks will be processed.
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();  // Wait for all threads to complete
            }
        }
    }

private:
    boost::asio::io_context ioContext;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard;
    std::vector<std::thread> workers;
};

BoxArtManager::BoxArtManager() :
    m_BoxArtDir(Path::getBoxArtCacheDir()),
    m_threadPool(new ThreadPool(4))
{
    if (!std::filesystem::exists(m_BoxArtDir)) 
    {
        std::filesystem::create_directories(m_BoxArtDir);
    }
}

BoxArtManager::~BoxArtManager()
{
    m_threadPool->wait();
    delete m_threadPool;
}

std::string
BoxArtManager::getFilePathForBoxArt(NvComputer* computer, int appId)
{
    std::filesystem::path dir = m_BoxArtDir;
    dir /= computer->uuid;

    // Create the cache directory if it did not already exist
    if (!std::filesystem::exists(dir)) 
    {
        std::filesystem::create_directories(dir);
    }

    std::filesystem::path filePath = dir / (std::to_string(appId) + ".png");

    return filePath.string();
}

class NetworkBoxArtLoadTask
{
public:

    static void run(BoxArtManager* bam, NvComputer* computer, NvApp& app)
    {
        std::string image = bam->loadBoxArtFromNetwork(computer, app.id);
        if (image.empty()) {
            // Give it another shot if it fails once
            image = bam->loadBoxArtFromNetwork(computer, app.id);
        }
    }
};

std::string BoxArtManager::loadBoxArt(NvComputer* computer, NvApp& app)
{
    // Try to open the cached file if it exists and contains data
    std::string cacheFile = getFilePathForBoxArt(computer, app.id);
    if (std::filesystem::exists(cacheFile) && std::filesystem::file_size(cacheFile) > 0) 
    {
        std::ifstream file(cacheFile, std::ios::binary);
        if (!file.is_open())
        {
            LOG(WARNING) << "Open file failed: " << cacheFile;
            return "";
        }

        std::vector<unsigned char> fileData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string encodeData =  "data:image/png;base64," + Base64::encode(fileData);
        return encodeData;
    }

    // If we get here, we need to fetch asynchronously.
    // Kick off a worker on our thread pool to do just that.
    m_threadPool->enqueue(&NetworkBoxArtLoadTask::run, this, computer, app);

    // Return the placeholder then we can notify the caller
    // later when the real image is ready.
    return "";
}

void BoxArtManager::deleteBoxArt(NvComputer* computer)
{
    std::filesystem::path dir(Path::getBoxArtCacheDir());
    dir /= computer->uuid;

    // Delete everything in this computer's box art directory
    if (std::filesystem::exists(dir))
    {
        std::filesystem::remove_all(dir);
    }
}

std::string BoxArtManager::loadBoxArtFromNetwork(NvComputer* computer, int appId)
{
    NvHTTP http(computer);

    std::string cachePath = getFilePathForBoxArt(computer, appId);
    std::string image;
    try {
        image = http.getBoxArt(appId);
    } catch (...) {}

    // Cache the box art on disk if it loaded
    if (!image.empty()) 
    {
        std::ofstream file(cachePath, std::ios::binary);
        if (file.is_open()) 
        {
            file << image;
            file.close();
            return cachePath;
        }
        else 
        {
            try 
            {
                std::filesystem::remove(cachePath);
            }
            catch (const std::filesystem::filesystem_error& ex)
            {
                LOG(WARNING) << "Error removing file: " << ex.what();
            }
        }
    }

    return "";
}
