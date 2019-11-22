/*******************************************************************************
* Copyright 2019 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "sycl/sycl_buffer_memory_storage.hpp"

#include <CL/sycl.hpp>

#include "common/guard_manager.hpp"
#include "common/memory.hpp"

namespace dnnl {
namespace impl {
namespace sycl {

struct map_tag;

sycl_buffer_memory_storage_t::sycl_buffer_memory_storage_t(
        engine_t *engine, unsigned flags, size_t size, void *handle)
    : sycl_memory_storage_base_t(engine), buffer_(nullptr) {
    // Do not allocate memory if one of these is true:
    // 1) size is 0
    // 2) handle is nullptr and flags have use_runtime_ptr
    if ((size == 0) || (!handle && !(flags & memory_flags_t::alloc))) return;
    if (flags & memory_flags_t::alloc) {
        buffer_.reset(new buffer_u8_t(cl::sycl::range<1>(size)));
    } else if (flags & memory_flags_t::use_runtime_ptr) {
        auto &buf_u8 = *static_cast<buffer_u8_t *>(handle);
        buffer_.reset(new buffer_u8_t(buf_u8));
    } else {
        assert(!"not expected");
    }
}

status_t sycl_buffer_memory_storage_t::map_data(void **mapped_ptr) const {
    if (!buffer_) {
        *mapped_ptr = nullptr;
        return status::success;
    }

    auto &guard_manager = guard_manager_t<map_tag>::instance();

    auto acc = buffer_->get_access<cl::sycl::access::mode::read_write>();
    auto *acc_ptr = new decltype(acc)(acc);
    *mapped_ptr = static_cast<void *>(acc_ptr->get_pointer());
    auto unmap_callback = [=]() { delete acc_ptr; };
    return guard_manager.enter(this, unmap_callback);
}

status_t sycl_buffer_memory_storage_t::unmap_data(void *mapped_ptr) const {
    if (!mapped_ptr) return status::success;

    auto &guard_manager = guard_manager_t<map_tag>::instance();
    return guard_manager.exit(this);
}

std::unique_ptr<memory_storage_t> sycl_buffer_memory_storage_t::get_sub_storage(
        size_t offset, size_t size) const {
    auto storage = new sycl_buffer_memory_storage_t(
            this->engine(), memory_flags_t::use_runtime_ptr, 0, nullptr);
    if (engine()->kind() == engine_kind::cpu) {
        storage->buffer_ = buffer_;
        storage->base_offset_ = offset;
    } else {
        auto sub_buffer
                = buffer_ ? new buffer_u8_t(*buffer_, offset, size) : nullptr;
        storage->buffer_.reset(sub_buffer);
        storage->base_offset_ = 0;
    }

    return std::unique_ptr<memory_storage_t>(storage);
}

std::unique_ptr<memory_storage_t> sycl_buffer_memory_storage_t::clone() const {
    auto storage = new sycl_buffer_memory_storage_t(
            engine(), memory_flags_t::use_runtime_ptr, 0, nullptr);
    storage->buffer_ = buffer_;
    storage->base_offset_ = base_offset_;
    return std::unique_ptr<memory_storage_t>(storage);
}

} // namespace sycl
} // namespace impl
} // namespace dnnl