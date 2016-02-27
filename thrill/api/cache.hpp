/*******************************************************************************
 * thrill/api/cache.hpp
 *
 * Part of Project Thrill - http://project-thrill.org
 *
 * Copyright (C) 2015 Sebastian Lamm <seba.lamm@gmail.com>
 *
 * All rights reserved. Published under the BSD-2 license in the LICENSE file.
 ******************************************************************************/

#pragma once
#ifndef THRILL_API_CACHE_HEADER
#define THRILL_API_CACHE_HEADER

#include <thrill/api/dia.hpp>
#include <thrill/api/dia_node.hpp>
#include <thrill/data/file.hpp>

#include <string>
#include <vector>

namespace thrill {
namespace api {

//! \addtogroup api Interface
//! \{

/*!
 * A DOpNode which caches all items in an external file.
 */
template <typename ValueType, typename ParentDIA>
class CacheNode final : public DIANode<ValueType>
{
public:
    using Super = DIANode<ValueType>;
    using Super::context_;

    /*!
     * Constructor for a LOpNode. Sets the Context, parents and stack.
     */
    explicit CacheNode(const ParentDIA& parent)
        : Super(parent.ctx(), "Cache", { parent.id() }, { parent.node() })
    {
        // CacheNodes are kept by default.
        Super::consume_counter_ = Super::never_consume_;

        auto save_fn = [this](const ValueType& input) {
                           writer_.Put(input);
                       };
        auto lop_chain = parent.stack().push(save_fn).fold();
        parent.node()->AddChild(this, lop_chain);
    }

    void StopPreOp(size_t /* id */) final {
        // Push local elements to children
        writer_.Close();
    }

    void Execute() final { }

    void PushData(bool consume) final {
        data::File::Reader reader = file_.GetReader(consume);
        for (size_t i = 0; i < file_.num_items(); ++i) {
            this->PushItem(reader.Next<ValueType>());
        }
    }

private:
    //! Local data file
    data::File file_ { context_.GetFile() };
    //! Data writer to local file (only active in PreOp).
    data::File::Writer writer_ { file_.GetWriter() };
};

template <typename ValueType, typename Stack>
auto DIA<ValueType, Stack>::Cache() const {
    assert(IsValid());

    using CacheNode = api::CacheNode<ValueType, DIA>;

    auto shared_node = std::make_shared<CacheNode>(*this);

    return DIA<ValueType>(shared_node);
}

//! \}

} // namespace api
} // namespace thrill

#endif // !THRILL_API_CACHE_HEADER

/******************************************************************************/
