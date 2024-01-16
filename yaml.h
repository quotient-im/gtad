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
#include <filesystem>
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

        template <typename VV>
        bool operator==(const iterator_base<VV>& rhs) const
        {
            return _impl == rhs._impl &&
                ((_fileName && rhs._fileName && *_fileName == *rhs._fileName) ||
                    (!_fileName && !rhs._fileName));
        }

        value_type operator*() const { return value_type(*_impl, _fileName); }

        proxy operator->() const { return proxy(**this); }

    private:
        iter_impl_t _impl;
        std::shared_ptr<std::string> _fileName;
};

template <typename ValueT, YAML::NodeType::value NodeTypeV, typename KeyT>
class YamlNodeTemplate;
class YamlNode;
struct YamlNodePair;

using YamlMap = YamlNodeTemplate<YamlNodePair, YAML::NodeType::Map, std::string_view>;
using YamlSequence = YamlNodeTemplate<YamlNode, YAML::NodeType::Sequence, size_t>;

class YamlNode : public YAML::Node
{
    public:
        YamlNode() = default;
        YamlNode(const YAML::Node& rhs, std::shared_ptr<std::string> fileName)
            : Node(rhs), _fileName(std::move(fileName)) { }

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

        template <typename T, typename DT = T>
        T tryAs(const DT& defaultVal = {}) const
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

struct YamlNodePair : public std::pair<YamlNode, YamlNode>
{
    YamlNodePair(const YamlNodePair&) = default;
    YamlNodePair(YamlNodePair&&) = default;
    YamlNodePair(const std::pair<YAML::Node, YAML::Node>& p,
             const std::shared_ptr<std::string>& fileName)
        : pair(YamlNode(p.first, fileName), YamlNode(p.second, fileName))
    { }
    ~YamlNodePair() = default;
    void operator=(const YamlNodePair&) = delete;
    void operator=(YamlNodePair&&) = delete;
};

struct YamlException : Exception
{
    explicit YamlException(const YamlNode& node, std::string_view msg) noexcept
        : Exception(node.location().append(": ").append(msg))
    {}
};

template <typename ValueT, YAML::NodeType::value NodeTypeV, typename KeyT>
class YamlNodeTemplate : public YamlNode
{
    protected:
        using my_type = YamlNodeTemplate<ValueT, NodeTypeV, KeyT>;

    public:
        using value_type = ValueT;
        using iterator = iterator_base<value_type>;
        using const_iterator = iterator_base<const value_type>;

        YamlNodeTemplate() = default;
        YamlNodeTemplate(const my_type&) = default;
        YamlNodeTemplate(my_type&&) = default;
        YamlNodeTemplate(const YamlNode& yn)
            : YamlNode(yn)
        {
            if (IsDefined())
                checkType(NodeTypeV);
        }
        YamlNodeTemplate(YamlNode&& yn)
            : YamlNode(yn)
        {
            if (IsDefined())
                checkType(NodeTypeV);
        }
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

        const YamlNode operator[](KeyT key) const
        {
            return { YAML::Node::operator[](key), _fileName };
        }

        YamlNode operator[](KeyT key)
        {
            return {YAML::Node::operator[](key), _fileName};
        }

        const YamlNode get(KeyT subnodeKey) const
        {
            auto subnode = (*this)[subnodeKey];
            if (subnode.IsDefined())
                return subnode;

            // Using stringstream to dump subnodeKey whether it's size_t or string_view
            throw YamlException(
                *this, (std::stringstream() << "subnode " << subnodeKey << " is undefined").view());
        }

        template <typename AsT>
        AsT get(KeyT key) const
        {
            return get(key).template as<AsT>();
        }

        value_type front() const
        {
            if (empty())
                throw YamlException(*this, "Trying to get an element from an empty container");
            return *begin();
        }
};

std::vector<std::string> asStrings(const YamlSequence& seq);

YamlMap loadYamlFromFile(const std::filesystem::path& fileName,
                         const pair_vector_t<std::string>& replacePairs = {});
