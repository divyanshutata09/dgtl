/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <typeinfo>
#include <utility>
#include <mutex>
#include <typeindex>
#include <unordered_map>

export module dgtl.ir_analysis_manager;

import dgtl.ir_analysis;
import dgtl.ir_contracts;

export namespace dgtl::ir::analysis {

class AnalysisManager {

    struct Entry { AnalysisStamp stamp{}; std::type_index type{typeid(void)}; std::shared_ptr<void> value{}; };
    mutable std::mutex mutex_;
    std::unordered_map<AnalysisKey, Entry, std::function<std::size_t(const AnalysisKey&)>> cache_;
public:
    AnalysisManager() : cache_(0, [](const AnalysisKey& k) { return std::hash<std::uint64_t>{}((std::uint64_t(k.function_id)<<16) ^ std::uint16_t(k.kind)); }) {}
    void invalidate(std::uint64_t function_id, contract::Preservation changed, std::uint64_t epoch) {
        std::lock_guard lock(mutex_);
        for (auto it=cache_.begin(); it!=cache_.end();) {
            if (it->first.function_id==function_id && invalidated(it->second.stamp,{changed,epoch})) it=cache_.erase(it); else ++it;
        }
    }
    void invalidate_all(contract::Preservation changed) {
        std::lock_guard lock(mutex_);
        for (auto it=cache_.begin(); it!=cache_.end();) if (static_cast<std::uint32_t>(it->second.stamp.dependencies & changed)) it=cache_.erase(it); else ++it;
    }
    template<class T, class Factory> std::shared_ptr<const T> get_or_compute(AnalysisKey key, AnalysisStamp stamp, Factory&& factory) {
        std::lock_guard lock(mutex_);
        auto it=cache_.find(key);
        if (it!=cache_.end() && it->second.type==std::type_index(typeid(T)) && it->second.stamp.ir_epoch==stamp.ir_epoch && it->second.stamp.semantic_hash==stamp.semantic_hash)
            return std::static_pointer_cast<const T>(it->second.value);
        auto value=std::make_shared<T>(std::forward<Factory>(factory)());
        cache_[key]=Entry{stamp,std::type_index(typeid(T)),value}; return value;
    }
    void clear() { std::lock_guard lock(mutex_); cache_.clear(); }
};
}


