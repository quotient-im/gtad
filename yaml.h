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

#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/convert.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/detail/impl.h>

#include <iostream>

// Mostly taken from yaml-cpp but stores and adds fileName to the returned values
// so that it could be used with YamlNode
template <typename V>
class iterator_base
{
        // Transfer the 'const' from V (if it's there) to iterator_value
        using iter_impl_t = YAML::detail::iterator_base<
            typename std::conditional<std::is_const<V>::value,
                                      const YAML::detail::iterator_value,
                                      YAML::detail::iterator_value>::type>;

        struct proxy
        {
            explicit proxy(const V& x) : m_ref(x) {}
            V* operator->() { return std::addressof(m_ref); }
            operator V*() { return std::addressof(m_ref); }

            V m_ref;
        };

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = V;
        using difference_type = std::ptrdiff_t;
        using pointer = V*;
        using reference = V;

        iterator_base(iter_impl_t iter, std::string fileName)
            : _impl(std::move(iter)), _fileName(std::move(fileName))
        { }

        operator iterator_base<const V>() const
        {
            return { _impl, _fileName };
        }

        iterator_base<V>& operator++() { ++_impl; return *this; }
        iterator_base<V> operator++(int) { return { _impl++, _fileName }; }

        template <typename IterT>
        bool operator==(const IterT& rhs)
        {
            return _impl == rhs._impl && _fileName == rhs._fileName;
        }
        template <typename IterT>
        bool operator!=(const IterT& rhs)
        {
            return !(*this == rhs);
        }

        value_type operator*() const { return value_type(*_impl, _fileName); }

        proxy operator->() const { return proxy(**this); }

    private:
        iter_impl_t _impl;
        std::string _fileName;
};

class YamlNode : public YAML::Node
{
    public:
        explicit YamlNode(const std::string& fileName); // reads from the file
        YamlNode(const YAML::Node& rhs, std::string fileName)
            : Node(rhs), _fileName(std::move(fileName)) { }

        std::string fileName() const { return _fileName; }
        class Location
        {
            friend class YamlNode;
            explicit Location(const YamlNode& yn) : _node(yn) { }
            const YamlNode& _node;

            template <typename StreamT>
            friend StreamT& operator<<(StreamT& os, const YamlNode::Location& l)
            {
                return os << l._node.fileName() << ":" << l._node.Mark().line + 1;
            }
        };
        Location location() const { return Location(*this); }

        template <typename KeyT>
        YamlNode operator[](KeyT&& key) const
        {
            return { YAML::Node::operator[](std::forward<KeyT>(key)), fileName() };
        }

    protected:
        void assert(YAML::NodeType::value checkedType) const;

        template <typename ReturnT, typename KeyT>
        ReturnT get(KeyT&& subnodeKey, YAML::NodeType::value checkType,
                    bool allowNonexistent = false) const
        {
            auto subnode = (*this)[std::forward<KeyT>(subnodeKey)];
            if (subnode.IsDefined())
            {
                subnode.assert(checkType);
                return subnode;
            }
            if (allowNonexistent)
                return subnode;

            std::cerr << location() << ": "
                      << subnodeKey << " is undefined" << std::endl;
            structureFail();
        }

        [[noreturn]] void structureFail() const;
    private:
        std::string _fileName;
};

class YamlSequence : public YamlNode
{
    public:
        YamlSequence(const YamlNode& yn) : YamlNode(yn) { }

        YamlSequence(YamlNode&& yn) : YamlNode(std::move(yn)) { }

        YamlNode operator[](size_t idx) const
        {
            return YamlNode::operator[](idx);
        }

        template <typename ReturnT>
        ReturnT get(size_t subnodeIdx, YAML::NodeType::value checkType,
                    bool allowNonexistent = false) const
        {
            return YamlNode::get<ReturnT>(subnodeIdx, checkType, allowNonexistent);
        }

        using iterator = iterator_base<YamlNode>;
        using const_iterator = iterator_base<const YamlNode>;

        const_iterator begin() const
        {
            return const_iterator(YAML::Node::begin(), fileName());
        }
        iterator begin() { return iterator(YAML::Node::begin(), fileName()); }

        const_iterator end() const
        {
            return const_iterator(YAML::Node::end(), fileName());
        }
        iterator end() { return iterator(YAML::Node::end(), fileName()); }
};

class YamlMap : public YamlNode
{
    public:
        YamlMap(const YamlNode& yn) : YamlNode(yn) { }

        YamlMap(YamlNode&& yn) : YamlNode(std::move(yn)) { }

        template <typename KeyT>
        YamlNode getScalar(KeyT&& subnodeKey, bool allowNonexistent = false) const
        {
            return get<YamlNode>(std::forward<KeyT>(subnodeKey),
                                 YAML::NodeType::Scalar, allowNonexistent);
        }

        template <typename KeyT>
        YamlSequence getSequence(KeyT&& subnodeKey, bool allowNonexistent = false) const
        {
            return get<YamlSequence>(std::forward<KeyT>(subnodeKey),
                                     YAML::NodeType::Sequence, allowNonexistent);
        }

        template <typename KeyT>
        YamlMap getMap(KeyT&& subnodeKey, bool allowNonexistent = false) const
        {
            return get<YamlMap>(std::forward<KeyT>(subnodeKey),
                                YAML::NodeType::Map, allowNonexistent);
        }

        struct NodePair : public std::pair<YamlNode, YamlNode>
        {
            NodePair(const NodePair&) = default;
            NodePair(NodePair&&) = default;
            NodePair(const std::pair<YAML::Node, YAML::Node>& p,
                     const std::string& fileName)
                : pair(YamlNode(p.first, fileName), YamlNode(p.second, fileName))
            { }
        };

        using iterator = iterator_base<NodePair>;
        using const_iterator = iterator_base<const NodePair>;

        const_iterator begin() const
        {
            return const_iterator(YAML::Node::begin(), fileName());
        }
        iterator begin() { return iterator(YAML::Node::begin(), fileName()); }

        const_iterator end() const
        {
            return const_iterator(YAML::Node::end(), fileName());
        }
        iterator end() { return iterator(YAML::Node::end(), fileName()); }
};

