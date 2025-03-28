/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstring>
#include <fstream>
#include <sstream>
#include "rtconfig.h"
#include "lottiemodel.h"

using namespace rlottie::internal;

#ifndef USING_MINI_RLOTTIE
#ifdef LOTTIE_CACHE_SUPPORT

#include <mutex>
#include <unordered_map>

class ModelCache {
public:
    static ModelCache &instance()
    {
        static ModelCache singleton;
        return singleton;
    }
    std::shared_ptr<model::Composition> find(const std::string &key)
    {
#ifdef MT_CPPLIB    
        std::lock_guard<std::mutex> guard(mMutex);
#endif
        if (!mcacheSize) return nullptr;

        auto search = mHash.find(key);

        return (search != mHash.end()) ? search->second : nullptr;
    }
    void add(const std::string &key, std::shared_ptr<model::Composition> value)
    {
#ifdef MT_CPPLIB    
        std::lock_guard<std::mutex> guard(mMutex);
#endif
        if (!mcacheSize) return;

        //@TODO just remove the 1st element
        // not the best of LRU logic
        if (mcacheSize == mHash.size()) mHash.erase(mHash.cbegin());

        mHash[key] = std::move(value);
    }

    void configureCacheSize(size_t cacheSize)
    {
#ifdef MT_CPPLIB    
        std::lock_guard<std::mutex> guard(mMutex);
#endif
        mcacheSize = cacheSize;

        if (!mcacheSize) mHash.clear();
    }

private:
    ModelCache() = default;

    std::unordered_map<std::string, std::shared_ptr<model::Composition>> mHash;
#ifdef MT_CPPLIB   
    std::mutex                                                           mMutex;
#endif
    size_t mcacheSize{10};
};

#else

class ModelCache {
public:
    static ModelCache &instance()
    {
        static ModelCache singleton;
        return singleton;
    }
    std::shared_ptr<model::Composition> find(const std::string &)
    {
        return nullptr;
    }
    void add(const std::string &, std::shared_ptr<model::Composition>) {}
    void configureCacheSize(size_t) {}
};

#endif

#endif
static std::string dirname(const std::string &path)
{
    const char *ptr = strrchr(path.c_str(), '/');
#ifdef _WIN32
    if (ptr) ptr = strrchr(ptr + 1, '\\');
#endif
    int len = int(ptr + 1 - path.c_str());  // +1 to include '/'
    return std::string(path, 0, len);
}

#ifndef USING_MINI_RLOTTIE
void model::configureModelCacheSize(size_t cacheSize)
{
    ModelCache::instance().configureCacheSize(cacheSize);
}
#endif
std::shared_ptr<model::Composition> model::loadFromFile(const std::string &path,
                                                        bool cachePolicy)
{
#ifndef USING_MINI_RLOTTIE
    if (cachePolicy) {
        auto obj = ModelCache::instance().find(path);
        if (obj) return obj;
    }
#endif
    std::ifstream f;
    f.open(path);

    if (!f.is_open()) {
        vCritical << "failed to open file = " << path.c_str();
        return {};
    } else {
        // Not using string here since it's over allocating up to 2x the input size
        f.seekg(0, std::ios_base::end);
        std::streampos sz = f.tellg();
        f.seekg(0, std::ios_base::beg);

        if (!sz) return {};

        char * buf = new char[(uint32_t)(sz + std::streampos{1})];
        f.read(buf, sz);
        f.close();

#ifdef LOTTIE_JSON_SUPPORT
        auto obj = internal::model::parse(buf, sz, dirname(path));
#else
        buf[(int)sz] = 0;
        auto obj = internal::model::parse(buf, dirname(path));
#endif
#ifndef USING_MINI_RLOTTIE
        if (obj && cachePolicy) ModelCache::instance().add(path, obj);
#endif
        delete[] buf;
        return obj;
    }
}

std::shared_ptr<model::Composition> model::loadFromData(
    std::string jsonData, const std::string &key, std::string resourcePath,
    bool cachePolicy)
{
#ifndef USING_MINI_RLOTTIE
    if (cachePolicy) {
        auto obj = ModelCache::instance().find(key);
        if (obj) return obj;
    }
#endif
#ifdef LOTTIE_JSON_SUPPORT
    auto obj = internal::model::parse(jsonData.c_str(), jsonData.length(),
                                      std::move(resourcePath));
#else
    auto obj = internal::model::parse(const_cast<char*>(jsonData.c_str()), std::move(resourcePath));
#endif
#ifndef USING_MINI_RLOTTIE
    if (obj && cachePolicy) ModelCache::instance().add(key, obj);
#endif
    return obj;
}
#ifndef USING_MINI_RLOTTIE
std::shared_ptr<model::Composition> model::loadFromData(
    std::string jsonData, std::string resourcePath, model::ColorFilter filter)
{
#ifdef LOTTIE_JSON_SUPPORT
    return internal::model::parse(jsonData.c_str(), jsonData.length(),
                                  std::move(resourcePath), std::move(filter));
#else
    return internal::model::parse(const_cast<char*>(jsonData.c_str()),
                                  std::move(resourcePath), std::move(filter));
#endif
}
#endif
std::shared_ptr<model::Composition> model::loadFromROData(const char * data, const size_t len, const char * resourcePath)
{
#ifdef LOTTIE_JSON_SUPPORT
    return internal::model::parse(data, len, resourcePath);
#else
    return nullptr;
#endif
}
