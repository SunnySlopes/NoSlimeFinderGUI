// SPDX-License-Identifier: MIT
// Copyright (c) 2026 SunnySlopes

/*
 * NoSlimeFinderGUI — thin JNI shim.
 *
 * This file contains no search logic: every call is forwarded to the
 * NoSlimeFinderBase public C API (include/noslimefinder/base.h). The base
 * repository is the `NoSlimeFinderBase` git submodule and supplies both the
 * static library linked here and the standalone CLI executable.
 */

#include "NoSlimeFinderBridge.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "noslimefinder/base.h"

namespace
{
std::mutex g_search_mutex;

int mc_from_string(JNIEnv *env, jstring value)
{
    if (value == nullptr)
        return noslime_newest_mc();

    int mc = -1;
    const char *s = env->GetStringUTFChars(value, nullptr);
    if (s != nullptr)
    {
        mc = noslime_parse_mc(s);
        env->ReleaseStringUTFChars(value, s);
    }
    return mc >= 0 ? mc : noslime_newest_mc();
}
} // namespace

JNIEXPORT jintArray JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_slimeSearch
    (JNIEnv *env, jclass, jlong seed, jint minX, jint maxX, jint minZ,
     jint maxZ, jint threads, jstring mcVersion, jint biomeY,
     jintArray avoidIds, jint maxResults, jint minArea)
{
    std::lock_guard<std::mutex> lock(g_search_mutex);

    std::vector<int32_t> avoid;
    if (avoidIds != nullptr)
    {
        const jsize n = env->GetArrayLength(avoidIds);
        if (n > 0)
        {
            // JNI reads into jint*, which is not int32_t* on every platform
            // (jint is `long` on Windows), so marshal through a jint buffer.
            std::vector<jint> ids(static_cast<size_t>(n));
            env->GetIntArrayRegion(avoidIds, 0, n, ids.data());
            avoid.assign(ids.begin(), ids.end());
        }
    }

    NoSlimeParams p{};
    p.seed = seed;
    p.afk_x0 = minX;
    p.afk_x1 = maxX;
    p.afk_z0 = minZ;
    p.afk_z1 = maxZ;
    p.threads = threads;
    p.biome_y = biomeY;
    p.max_results = maxResults;
    p.min_area = minArea;
    p.candidate_limit = 0;
    p.mc = mc_from_string(env, mcVersion);
    p.avoid_ids = avoid.empty() ? nullptr : avoid.data();
    p.avoid_count = static_cast<int32_t>(avoid.size());

    const int rc = noslime_run(&p);
    if (rc != 0)
        return nullptr;

    const int32_t count = noslime_result_count();
    std::vector<NoSlimeResult> results(
        static_cast<size_t>(std::max<int32_t>(0, count)));
    if (count > 0)
        noslime_get_results(results.data(), count);

    std::vector<jint> flat;
    flat.reserve(results.size() * 7);
    for (const NoSlimeResult &r : results)
    {
        flat.push_back(static_cast<jint>(r.x));
        flat.push_back(static_cast<jint>(r.z));
        flat.push_back(static_cast<jint>(r.area));
        flat.push_back(static_cast<jint>(r.slime));
        flat.push_back(static_cast<jint>(r.chunks));
        flat.push_back(static_cast<jint>(r.avoid_x));
        flat.push_back(static_cast<jint>(r.avoid_y));
    }

    jintArray out = env->NewIntArray(static_cast<jsize>(flat.size()));
    if (out == nullptr)
        return nullptr;
    if (!flat.empty())
        env->SetIntArrayRegion(out, 0, static_cast<jsize>(flat.size()), flat.data());
    return out;
}

JNIEXPORT jobjectArray JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_listBiomes
    (JNIEnv *env, jclass, jstring mcVersion)
{
    const int mc = mc_from_string(env, mcVersion);
    const int32_t count = noslime_list_biomes(mc, nullptr, 0);
    if (count <= 0)
        return nullptr;

    std::vector<NoSlimeBiome> biomes(static_cast<size_t>(count));
    const int32_t filled = noslime_list_biomes(mc, biomes.data(), count);
    if (filled <= 0)
        return nullptr;

    jclass stringClass = env->FindClass("java/lang/String");
    if (stringClass == nullptr)
        return nullptr;
    jobjectArray arr =
        env->NewObjectArray(static_cast<jsize>(filled), stringClass, nullptr);
    if (arr == nullptr)
        return nullptr;

    for (jsize i = 0; i < static_cast<jsize>(filled); ++i)
    {
        const std::string item =
            std::to_string(biomes[static_cast<size_t>(i)].id) + "|" +
            biomes[static_cast<size_t>(i)].name;
        jstring s = env->NewStringUTF(item.c_str());
        env->SetObjectArrayElement(arr, i, s);
        env->DeleteLocalRef(s);
    }
    return arr;
}

JNIEXPORT jlongArray JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_getSearchProgress
    (JNIEnv *env, jclass)
{
    int64_t p[8] = {0};
    noslime_get_progress(p);
    jlongArray out = env->NewLongArray(8);
    if (out == nullptr)
        return nullptr;
    env->SetLongArrayRegion(out, 0, 8, reinterpret_cast<const jlong *>(p));
    return out;
}

JNIEXPORT jboolean JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_pause(JNIEnv *, jclass)
{
    return noslime_pause() == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_resume(JNIEnv *, jclass)
{
    return noslime_resume() == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_stop(JNIEnv *, jclass)
{
    return noslime_cancel() == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_sunnyslopes_noslimefinder_NoSlimeFinderBridge_lastError(JNIEnv *env, jclass)
{
    return env->NewStringUTF(noslime_last_error());
}
