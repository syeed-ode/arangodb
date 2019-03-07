////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_GRAPH_EDGEDOCUMENTTOKEN_H
#define ARANGOD_GRAPH_EDGEDOCUMENTTOKEN_H 1

#include "Basics/Common.h"
#include "Basics/StringRef.h"
#include "StorageEngine/DocumentIdentifierToken.h"
#include "VocBase/voc-types.h"

namespace arangodb {

namespace graph {

/// @brief Pure virtual abstract class to uniquely identify an edge
struct EdgeDocumentToken {
  EdgeDocumentToken() noexcept
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
      : _data(0, DocumentIdentifierToken()), _type(TokenType::NONE) {
  }
#else
      : _data(0, DocumentIdentifierToken()) {
  }
#endif

  EdgeDocumentToken(EdgeDocumentToken&& edtkn) noexcept {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    _type = edtkn._type;
#endif
    memmove(&(this->_data), &(edtkn._data), sizeof(TokenData));
  }

  EdgeDocumentToken(EdgeDocumentToken const& edtkn) noexcept {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    _type = edtkn._type;
#endif
    memcpy(&(this->_data), &(edtkn._data), sizeof(TokenData));
  }

  EdgeDocumentToken(TRI_voc_cid_t const cid, DocumentIdentifierToken const token) noexcept
      : _data(cid, token) {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    _type = EdgeDocumentToken::TokenType::LOCAL;
#endif
  }

  EdgeDocumentToken(arangodb::velocypack::Slice const& edge) noexcept
      : _data(edge) {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    _type = EdgeDocumentToken::TokenType::COORDINATOR;
#endif
  }

  EdgeDocumentToken& operator=(EdgeDocumentToken&& edtkn) {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    _type = edtkn._type;
#endif
    memcpy(&(this->_data), &(edtkn._data), sizeof(TokenData));
    return *this;
  }

  TRI_voc_cid_t cid() const {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    TRI_ASSERT(_type == TokenType::LOCAL);
#endif
    TRI_ASSERT(_data.document.cid != 0);
    return _data.document.cid;
  }

  DocumentIdentifierToken token() const {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    TRI_ASSERT(_type == TokenType::LOCAL);
#endif
    TRI_ASSERT(_data.document.token != 0);
    return _data.document.token;
  }

  uint8_t const* vpack() const {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    TRI_ASSERT(_type == TokenType::COORDINATOR);
#endif
    TRI_ASSERT(_data.vpack);
    return _data.vpack;
  }

  bool equalsCoordinator(EdgeDocumentToken const& other) const {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    TRI_ASSERT(_type == TokenType::COORDINATOR);
#endif
    // FIXME:
    return velocypack::Slice(_data.vpack) == velocypack::Slice(other._data.vpack);
  }

  bool equalsLocal(EdgeDocumentToken const& other) const {
#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
    TRI_ASSERT(_type == TokenType::LOCAL);
#endif
    return _data.document.cid == other.cid() && _data.document.token == other.token();
  }

 private:
  /// Identifying information for an edge documents valid on one server
  /// only used on a dbserver or single server
  struct LocalDocument {
    TRI_voc_cid_t cid;
    DocumentIdentifierToken token;
    ~LocalDocument() {}
  };

  /// fixed size union, works for both single server and
  /// cluster case
  union TokenData {
    EdgeDocumentToken::LocalDocument document;
    uint8_t const* vpack;

    TokenData() { vpack = nullptr; }
    TokenData(velocypack::Slice const& edge) : vpack(edge.begin()) {
      TRI_ASSERT(!velocypack::Slice(vpack).isExternal());
    }
    TokenData(TRI_voc_cid_t cid, DocumentIdentifierToken tk) {
      document.cid = cid;
      document.token = tk;
    }
    ~TokenData() {}
  };

  TokenData _data;

#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
  enum TokenType : uint8_t { NONE, LOCAL, COORDINATOR };
  TokenType _type;
#endif
};
}  // namespace graph

}  // namespace arangodb
#endif
