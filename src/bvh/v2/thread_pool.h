#ifndef BVH_V2_THREAD_POOL_H
#define BVH_V2_THREAD_POOL_H

#include <pthread.h>
#include <vector>
#include <queue>
#include <functional>

namespace bvh::v2 {

class ThreadPool {
public:
    using Task = std::function<void(size_t)>;

    /// Creates a thread pool with the given number of threads (a value of 0 tries to autodetect
    /// the number of threads and uses that as a thread count).
    ThreadPool(size_t thread_count = 5) { start(thread_count); }

    ~ThreadPool() {
        wait();
        stop();
        join();
    }

    inline void push(Task&& fun);
    inline void wait();

    size_t get_thread_count() const { return threads_.size(); }

    bool is_failed() const { return failed_; }

private:
    static inline void* worker(void*);

    inline void start(size_t);
    inline void stop();
    inline void join();

    int busy_count_ = 0;
    bool should_stop_ = false;
    std::vector<pthread_t> threads_;
    std::queue<Task> tasks_;
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t avail_ = PTHREAD_COND_INITIALIZER;
    pthread_cond_t done_ = PTHREAD_COND_INITIALIZER;

    bool failed_ = false;
};

void ThreadPool::push(Task&& task) {
    {
        pthread_mutex_lock(&mutex_);
        tasks_.emplace(std::move(task));
    }
    pthread_cond_signal(&avail_);
}

void ThreadPool::wait() {
    pthread_mutex_lock(&mutex_);
    while (busy_count_ != 0 || !tasks_.empty()) {
        // printf("waiting for %d, %d tasks to finish\n",B busy_count_, tasks_.size());
        pthread_cond_wait(&done_, &mutex_);
    }
    pthread_mutex_unlock(&mutex_);
}

void* ThreadPool::worker(void* arg) {
    auto pool = static_cast<ThreadPool*>(arg);

    while (true) {
        Task task;
        {
            pthread_mutex_lock(&pool->mutex_);
            while (!pool->should_stop_ && pool->tasks_.empty()) {
                pthread_cond_wait(&pool->avail_, &pool->mutex_);
            }
            if (pool->should_stop_ && pool->tasks_.empty()) {
                pthread_mutex_unlock(&pool->mutex_);
                break;
            }
            task = std::move(pool->tasks_.front());
            pool->tasks_.pop();
            pool->busy_count_++;
            pthread_mutex_unlock(&pool->mutex_);
        }
        task(reinterpret_cast<size_t>(pthread_self()));

        {
            pthread_mutex_lock(&pool->mutex_);
            pool->busy_count_--;
            pthread_mutex_unlock(&pool->mutex_);
            pthread_cond_signal(&pool->done_);
        }
    }
    return nullptr;
}

void ThreadPool::start(size_t thread_count) {
    // if (thread_count == 0)
    //     thread_count = std::max(1u, std::thread::hardware_concurrency());
    threads_.resize(thread_count);
    for (size_t i = 0; i < thread_count; ++i) {
        int err = pthread_create(&threads_[i], nullptr, worker, this);
        if (err) {
            printf("Create pthread error. %s\n", strerror(err));
            failed_ = true;
            break;
        }
    }
}

void ThreadPool::stop() {
    pthread_mutex_lock(&mutex_);
    should_stop_ = true;
    pthread_mutex_unlock(&mutex_);
    pthread_cond_broadcast(&avail_);
}

void ThreadPool::join() {
    for (auto& thread : threads_)
        pthread_join(thread, nullptr);
}

} // namespace bvh::v2

#endif

