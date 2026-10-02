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

export module dgtl.ir_transaction;

import dgtl.ir.stream;

export namespace dgtl::ir {

class IRTransaction {

    IRStream* stream_{nullptr}; bool committed_{false};
public:
    explicit IRTransaction(IRStream& s) noexcept : stream_(&s) { stream_->begin_transaction(); }
    IRTransaction(const IRTransaction&)=delete; IRTransaction& operator=(const IRTransaction&)=delete;
    IRTransaction(IRTransaction&& o) noexcept : stream_(o.stream_), committed_(o.committed_) { o.stream_=nullptr; }
    ~IRTransaction() { if (stream_ && !committed_) stream_->abort_transaction(); }
    void commit() noexcept { if (stream_ && !committed_) { stream_->commit_transaction(); committed_=true; } }
    void abort() noexcept { if (stream_ && !committed_) { stream_->abort_transaction(); committed_=true; } }
    [[nodiscard]] bool committed() const noexcept { return committed_; }
};
}


