/******************************************************************************
 * Copyright (C) 2017 Kitsune Ral <kitsune-ral@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "util.h"

#include <yaml-cpp/node/convert.h>
#include <yaml-cpp/node/detail/impl.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/convert.h>

#include <utility>
#include <ranges>

class YamlNode;

struct YamlException : Exception {
    explicit YamlException(const YamlNode& node, std::string_view msg) noexcept;
};

template <YAML::NodeType::value NodeTypeV, typename KeyT, typename ItemT>
class YamlContainer;

template <typename ItemT = YamlNode>
using YamlMap = YamlContainer<YAML::NodeType::Map, std::string, ItemT>;
using YamlGenericMap = YamlContainer<YAML::NodeType::Map, YamlNode, YamlNode>;

template <typename ItemT = YamlNode>
using YamlSequence = YamlContainer<YAML::NodeType::Sequence, size_t, ItemT>;

class YamlNode : public YAML::Node {
public:
    struct Context {
        std::string fileName;
        YAML::Node rootNode;
    };
    // This constructor is templated to prevent accidental construction from YamlNode and descendants
    template <class NodeT = YAML::Node>
        requires std::is_same_v<std::decay_t<NodeT>, YAML::Node>
    YamlNode(const NodeT& n = {}, std::shared_ptr<Context> context = {})
        : YamlNode(n, context ? context : std::make_shared<Context>(), {})
    {
        Mark(); // Throw YAML::InvalidNode if n is invalid
    }
    static YamlNode fromFile(const std::string& fileName, const subst_list_t& replacePairs = {});

    const std::string& fileName() const { return _context->fileName; }
    YamlNode root() const { return {_context->rootNode, _context, AllowUndefined{}}; }
    std::string location() const
    {
        return fileName() + ':' + std::to_string(Mark().line + 1);
    }

    bool empty() const { return !IsDefined() || size() == 0; }

    template <typename T>
    T as() const
    {
        if constexpr (std::is_base_of_v<YamlNode, T>) {
            // The called constructor will check the existing node type if T is a YamlContainer<>
            return T(*this);
        } else {
            // YAML::Node::as<> and YAML::convert<> don't work with `const std::string` etc.
            using NonConstT = std::remove_const_t<T>;
            // YAML::Node::as<>() (YAML::convert<>, actually) doesn't expose expected node type
            // at build time; so we do some semi-heuristical checks before diving into as<> -
            // in theory, it may still throw its own exception if convert<T> has some unusual logic
            // but none of that exists in yaml-cpp so with stock yaml-cpp we should be totally good
            // 1. Try to deduce what YAML node type is expected
            static const auto ExpectedNodeType = YAML::convert<NonConstT>::encode({}).Type();
            checkType(ExpectedNodeType);
            // 2. Wrong dimensions is the other possible problem with convert<> specialisations
            // that come with yaml-cpp (in particular, std::array and std::pair)
            if constexpr (requires { std::tuple_size<T>::value; })
                if (size() != std::tuple_size_v<T>)
                    throw YamlException(*this, "Incorrect container size: expected "
                                                   + std::to_string(std::tuple_size_v<T>) + ", got "
                                                   + std::to_string(size()));
            return Node::as<NonConstT>(); // That's it! Not much can go wrong really
        }
    }

    //! \brief Resolve a Reference Object to the referred-to YAML value
    //!
    //! If the current node is a Reference Object (i.e. it has a key named `$ref`) this call
    //! attempts to resolve it to an entity referred to by the value under `$ref`. If the original
    //! Reference Object defines `summary` and/or `description` and the resolved object is a map,
    //! the values under these keys are applied to the resolved object. Finally, the resulting YAML
    //! node is converted to \p TargetT before returning.
    //! If the current node is not a map or has no `$ref` key, the function returns a shallow copy
    //! of that very node.
    //! \note Only local (within the same file) references are supported for now; a YamlException
    //!       will be thrown on non-local ones (i.e. that don't start with `#`).
    //! \note If the resolved entity is not a map, trying to apply `summary`/`description` to it
    //!       will trigger YamlException
    //! \sa https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#reference-object
    template <class TargetT = YamlMap<>>
    TargetT resolveRef() const
    {
        return doResolveRef().as<TargetT>();
    }

    void begin() const = delete;
    void begin() = delete;
    void end() const = delete;
    void end() = delete;

protected:
    struct AllowUndefined {};

    YamlNode(const Node& rhs, std::shared_ptr<Context> context, AllowUndefined)
        : Node(rhs), _context(std::move(context))
    {}

    YamlNode(const YamlNode& rhs, AllowUndefined)
        : Node(static_cast<const Node&>(rhs)), _context(rhs._context)
    {}

    template <typename T>
    static T as(const Node& rhs, std::shared_ptr<Context> context)
    {
        return YamlNode(rhs, context, AllowUndefined{}).template as<T>();
    }

    void checkType(YAML::NodeType::value checkedType) const;
    YamlNode doResolveRef() const;

    std::shared_ptr<Context> _context;

    template <class ContainerT>
    friend class iterator_base;
};

template <typename T>
class Optional : public std::optional<T> {
public:
    using std::optional<T>::optional;
    using std::optional<T>::operator bool;
};

template <std::derived_from<YamlNode> NodeT>
class Optional<NodeT> : private NodeT {
public:
    Optional(const YAML::Node& rhs, std::shared_ptr<YamlNode::Context> context)
        : NodeT(rhs, std::move(context), YamlNode::AllowUndefined{})
    {}

    using YamlNode::fileName, YamlNode::location, YamlNode::empty;
    using YAML::Node::operator bool, YAML::Node::operator!;

    NodeT& operator*()
    {
        YAML::Node::Mark(); // Trigger an exception if the node is undefined
        return *this;
    }
    const NodeT& operator*() const
    {
        YAML::Node::Mark(); // Trigger an exception if the node is undefined
        return *this;
    }
    NodeT* operator->() { return NodeT::IsDefined() ? this : nullptr; }
    const NodeT* operator->() const { return NodeT::IsDefined() ? this : nullptr; }

    using NodeT::begin, NodeT::end; // Only available in YamlContainer
};

using OptionalYamlNode = Optional<YamlNode>;

// Mostly taken from yaml-cpp but stores and adds fileName to the returned values
// so that it could be used with YamlNode's
template <class ContainerT>
class iterator_base {
public:
    using this_type = iterator_base<ContainerT>;
    using iterator_category = std::forward_iterator_tag;

    template <typename T>
    using apply_const_t = std::conditional_t<std::is_const_v<ContainerT>, const T, T>;

    using value_type = apply_const_t<typename ContainerT::value_type>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    // using reference = value_type; // Doesn't really make sense in our case

    iterator_base() = default; // Needed to satisfy range<ContainerT>

private:
    using iter_impl_t = YAML::detail::iterator_base<apply_const_t<YAML::detail::iterator_value>>;
    friend ContainerT;

    iterator_base(iter_impl_t iter, std::shared_ptr<YamlNode::Context> context)
        : _impl(std::move(iter)), _context(std::move(context))
    {}

    struct ArrowProxy {
        explicit ArrowProxy(const value_type& x) : m_ref(x) {}
        value_type* operator->() { return std::addressof(m_ref); }

        value_type m_ref;
    };

public:
    operator iterator_base<const ContainerT>() const
        requires(!std::is_const_v<ContainerT>)
    {
        return {_impl, _context};
    }

    this_type& operator++()
    {
        ++_impl;
        return *this;
    }
    this_type operator++(int) { return {_impl++, _context}; }

    auto operator==(const auto& rhs) const
        -> bool // Don't emit the function if rhs has a wrong type (=the body is ill-formed)
    {
        return _impl == rhs._impl
               && ((_context && rhs._context && _context->fileName == rhs._context->fileName)
                   || (!_context && !rhs._context));
    }

    value_type operator*() const
    {
        if constexpr (ContainerT::nodeType == YAML::NodeType::Sequence)
            return YamlNode::as<value_type>(*_impl, _context);
        else
            return std::pair{
                YamlNode::as<typename value_type::first_type>(_impl->first, _context),
                YamlNode::as<typename value_type::second_type>(_impl->second, _context)};
    }

    // NB: Don't try to store the returned result, or you will end up with a dangling proxy
    auto operator->() const { return ArrowProxy(**this); }

private:
    iter_impl_t _impl;
    std::shared_ptr<YamlNode::Context> _context;
};

template <YAML::NodeType::value NodeTypeV, typename KeyT, typename ItemT>
class YamlContainer : public YamlNode {
public:
    using this_type = YamlContainer<NodeTypeV, KeyT, ItemT>;
    static constexpr auto nodeType = NodeTypeV;
    static_assert(nodeType == YAML::NodeType::Sequence || nodeType == YAML::NodeType::Map);

    using key_view_type = std::conditional_t<std::is_same_v<KeyT, std::string>, std::string_view, KeyT>;
    using mapped_type = ItemT;
    //! The type returned from iterator dereferencing
    using value_type =
        std::conditional_t<nodeType == YAML::NodeType::Map, std::pair<KeyT, ItemT>, ItemT>;
    // using iterator = iterator_base<this_type>;
    using const_iterator = iterator_base<const this_type>;

    using YamlNode::YamlNode;
    explicit YamlContainer(const YamlNode& yn) : YamlNode(yn) { checkType(nodeType); }

    const_iterator begin() const { return const_iterator(Node::begin(), this->_context); }
    const_iterator end() const { return const_iterator(Node::end(), this->_context); }

    //! \brief Access an element in the container (requires existence)
    //!
    //! This is basically operator[] but throws on inexistent key even if mapped_type is derived
    //! from YamlNode. Recommended over operator[] when you need to ensure that the key actually
    //! exists.
    template <typename AsT = mapped_type>
    auto get(key_view_type key) const
    {
        const auto& subnode = Node::operator[](key);
        if (subnode.IsDefined())
            return YamlNode::as<AsT>(subnode, _context);
        throw YamlException(*this,
                            (std::stringstream() << "subnode " << key << " is undefined").view());
    }

    //! \brief Access an element in the container, or return a default value
    //!
    //! This get() overload doesn't throw if an element doesn't exist, instead returning the passed
    //! default value. If the key exists but the value at it cannot be converted to AsT, still
    //! throws; if you want to avoid exceptions and check everything in the calling code, call
    //! `get<YamlNode>(key, {})`; the returned value's type will be NodeType::Null if the element
    //! wasn't found.
    template <typename AsT = mapped_type, typename DT = AsT>
    AsT get(key_view_type key, DT&& defaultValue) const
    {
        if (const auto& subnode = Node::operator[](key); subnode.IsDefined())
            return YamlNode::as<AsT>(subnode, _context);
        return std::forward<DT>(defaultValue);
    }

    template <typename AsT = mapped_type>
    Optional<AsT> maybeGet(key_view_type key) const
    {
        const auto& subnode = Node::operator[](key);
        if constexpr (std::derived_from<AsT, YamlNode>)
            return Optional<AsT>(subnode, _context);
        else if (subnode.IsDefined())
            return Optional<AsT>(YamlNode::as<AsT>(subnode, _context));
        else
            return std::nullopt;
    }

    //! \brief Access an element in the container
    //!
    //! This call maintains YAML::Node conventions while it can, that is: if mapped_type is derived
    //! from YamlNode it is returned even if the node doesn't exist in the container; otherwise,
    //! an exception is thrown.
    const Optional<mapped_type> operator[](key_view_type key) const
    {
        return maybeGet<mapped_type>(key);
    }

    template <typename TargetT>
    bool maybeLoad(key_view_type key, TargetT* target) const
    {
        const auto optSubnode = maybeGet<TargetT>(key);
        if (optSubnode)
            *target = *optSubnode;
        return bool(optSubnode);
    }

    const_iterator::value_type front() const
    {
        if (this->empty())
            throw YamlException(*this, "Trying to get an element from an empty container");
        return *begin();
    }

protected:
    explicit YamlContainer(const YAML::Node& n, std::shared_ptr<Context> context, AllowUndefined)
        : YamlNode(n, context, AllowUndefined{})
    {
        if (IsDefined() && Type() != YAML::NodeType::Null) // Null is treated as empty container
            checkType(nodeType);
    }
};

//! \brief A range adaptor applying YamlContainer::resolveRef() to each value of the passed range
//!
//! This adaptor works with any range (not just YamlContainer) of YamlNodes, some of which may be
//! Reference Objects. It also works on YamlMaps of maybe-Reference Objects, copying keys as they
//! are and resolving references in values where applicable.
constexpr inline auto resolveRefs = std::views::transform([](const auto& v) {
    if constexpr (requires { v.second; })
        return std::pair{v.first, v.second.resolveRef()};
    else
        return v.resolveRef();
});

template <typename T>
struct YAML::convert<pair_vector_t<T>> {
    static Node encode(const pair_vector_t<T>& rhs)
    {
        // This is quite opinionated; a vector of pairs can be just as correctly stored as
        // a sequence. The original yaml-cpp logic, however, would produce a sequence of two-element
        // sequences for it, which is never appropriate in our cases, and rarely appropriate
        // elsewhere. Ideally encode() should've accepted a target type somehow; but it would make
        // for a completely different encoding API, and we don't even use that except in as().
        Node node(NodeType::Map);
        for (const auto& element : rhs)
            node.force_insert(element.first, element.second);
        return node;
    }

    static bool decode(const Node& node, pair_vector_t<T>& rhs)
    {
        if (!node.IsMap())
            return false;

        rhs.clear();
        rhs.reserve(node.size());
        for (const auto& element : node)
            rhs.emplace_back(element.first.Scalar(), element.second.as<T>());
        return true;
    }
};

