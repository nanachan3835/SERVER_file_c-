#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <condition_variable>
#include <mutex>
#include <queue>


inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

inline void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

inline bool is_same_dir(const std::string& path1, const std::string& path2) {
    size_t pos1 = path1.find_last_of('/');
    size_t pos2 = path2.find_last_of('/');

    // If only one of them has a directory, they are not in the same dir
    if ((pos1 == std::string::npos) != (pos2 == std::string::npos)) {
        return false;
    }

    // If both have no directory, consider them in the same (current) dir
    if (pos1 == std::string::npos && pos2 == std::string::npos) {
        return true;
    }

    // Compare substrings up to the last '/'
    std::string dir1 = path1.substr(0, pos1);
    std::string dir2 = path2.substr(0, pos2);

    return dir1 == dir2;
}


// Thread-safe queue
template <typename T>
class TSQueue {
private:
    // Underlying queue
    std::queue<T> m_queue;

    // mutex for thread synchronization
    std::mutex m_mutex;

    // Condition variable for signaling
    std::condition_variable m_cond;

public:
    // Push an element to the queue
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(item);
    }

    // // Pop an element from the queue
    // bool pop(T& item) {
    //     std::lock_guard<std::mutex> lock(m_mutex);
    //     if (m_queue.empty()) {
    //         return false;
    //     }
    //     item = m_queue.front();
    //     m_queue.pop();
    //     return true;
    // }

    // Check if queue is empty
    bool empty() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    // Get pointer to internal queue (for manual batch processing)
    std::queue<T>* get() {
        return &m_queue;
    }

    // Manually lock the queue
    void lock() {
        m_mutex.lock();
    }

    // Manually unlock the queue
    void unlock() {
        m_mutex.unlock();
    }
};