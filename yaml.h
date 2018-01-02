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

#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/convert.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/detail/impl.h>

#include <iostream>
#include <utility>

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

        iterator_base(iter_impl_t iter, std::shared_ptr<std::string> fileName)
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
            return _impl == rhs._impl && *_fileName == *rhs._fileName;
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
        std::shared_ptr<std::string> _fileName;
};

class YamlMap;
class YamlSequence;

class YamlNode : public YAML::Node
{
    public:
        YamlNode(const YAML::Node& rhs, std::shared_ptr<std::string> fileName)
            : Node(rhs), _fileName(move(fileName)) { }

        std::string fileName() const { return *_fileName; }

        std::string location() const noexcept
        {
            return fileName() + ':' + std::to_string(Mark().line + 1);
        }

        bool empty() const { return !IsDefined() || size() == 0; }

        template <typename T>
        T as() const
        {
            checkType(YAML::NodeType::Scalar);
            return YAML::Node::as<T>();
        }

        template <typename T, typename DT>
        T as(const DT& defaultVal) const
        {
            if (IsDefined())
                checkType(YAML::NodeType::Scalar);
            return YAML::Node::as<T>(defaultVal);
        }

        YamlMap asMap() const;

        YamlSequence asSequence() const;

    protected:
        void checkType(YAML::NodeType::value checkedType) const;

        std::shared_ptr<std::string> _fileName;
};

class YamlException : public Exception
{
    public:
        explicit YamlException(const YamlNode& node, std::string msg)
            : Exception(node.location() + ": " + msg)
        { }
        ~YamlException() override;
};

class YamlSequence : public YamlNode
{
    public:
        YamlSequence(const YamlSequence&) = default;
        YamlSequence(YamlSequence&&) = default;
        YamlSequence(const YamlNode& yn)
            : YamlNode(yn)
        {
            if (IsDefined())
                checkType(YAML::NodeType::Sequence);
        }

        YamlSequence(YamlNode&& yn)
            : YamlNode(std::move(yn))
        {
            if (IsDefined())
                checkType(YAML::NodeType::Sequence);
        }

        YamlNode operator[](size_t idx) const
        {
            return { YAML::Node::operator[](idx), _fileName };
        }

        YamlNode get(size_t subnodeIdx, bool allowNonexistent = false) const;

        using iterator = iterator_base<YamlNode>;
        using const_iterator = iterator_base<const YamlNode>;

        const_iterator begin() const
        {
            return const_iterator(YAML::Node::begin(), _fileName);
        }
        iterator begin() { return iterator(YAML::Node::begin(), _fileName); }

        const_iterator end() const
        {
            return const_iterator(YAML::Node::end(), _fileName);
        }
        iterator end() { return iterator(YAML::Node::end(), _fileName); }
};

class YamlMap : public YamlNode
{
    public:
        YamlMap(const YamlMap&) = default;
        YamlMap(YamlMap&&) = default;
        YamlMap(const YamlNode& yn)
            : YamlNode(yn)
        {
            if (IsDefined())
                checkType(YAML::NodeType::Map);
        }

        YamlMap(YamlNode&& yn)
            : YamlNode(std::move(yn))
        {
            if (IsDefined())
                checkType(YAML::NodeType::Map);
        }

        static YamlMap loadFromFile(const std::string& fileName,
            const pair_vector_t<std::string>& replacePairs = {});

        template <typename KeyT>
        YamlNode operator[](KeyT&& key) const
        {
            return { YAML::Node::operator[](std::forward<KeyT>(key)), _fileName };
        }

        template <typename KeyT>
        YamlNode get(KeyT&& subnodeKey, bool allowNonexistent = false) const
        {
            auto subnode = (*this)[std::forward<KeyT>(subnodeKey)];
            if (allowNonexistent || subnode.IsDefined())
                return subnode;

            throw YamlException(*this, std::string(subnodeKey) + " is undefined");
        }

        struct NodePair : public std::pair<YamlNode, YamlNode>
        {
            NodePair(const NodePair&) = default;
            NodePair(NodePair&&) = default;
            NodePair(const std::pair<YAML::Node, YAML::Node>& p,
                     std::shared_ptr<std::string> fileName)
                : pair(YamlNode(p.first, fileName), YamlNode(p.second, fileName))
            { }
        };

        using iterator = iterator_base<NodePair>;
        using const_iterator = iterator_base<const NodePair>;

        const_iterator begin() const
        {
            return const_iterator(YAML::Node::begin(), _fileName);
        }
        iterator begin() { return iterator(YAML::Node::begin(), _fileName); }

        const_iterator end() const
        {
            return const_iterator(YAML::Node::end(), _fileName);
        }
        iterator end() { return iterator(YAML::Node::end(), _fileName); }
};

