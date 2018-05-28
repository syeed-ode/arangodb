////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2018 ArangoDB GmbH, Cologne, Germany
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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "TransactionManager.h"

#include "Basics/ReadLocker.h"
#include "Basics/WriteLocker.h"
#include "StorageEngine/TransactionState.h"

using namespace arangodb;

// register a list of failed transactions
void TransactionManager::registerFailedTransactions(
    std::unordered_set<TRI_voc_tid_t> const& failedTransactions) {
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);

  for (auto const& it : failedTransactions) {
    size_t bucket = getBucket(it);

    WRITE_LOCKER(locker, _transactions[bucket]._lock);

    _transactions[bucket]._failedTransactions.emplace(it);
  }
}

// unregister a list of failed transactions
void TransactionManager::unregisterFailedTransactions(
    std::unordered_set<TRI_voc_tid_t> const& failedTransactions) {
    
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);

  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    WRITE_LOCKER(locker, _transactions[bucket]._lock);

    std::for_each(failedTransactions.begin(), failedTransactions.end(),
                [&](TRI_voc_tid_t id) { _transactions[bucket]._failedTransactions.erase(id); });
  }
}

void TransactionManager::registerTransaction(TransactionState& state, std::unique_ptr<TransactionData> data) {
  TRI_ASSERT(data != nullptr);
  _nrRunning.fetch_add(1, std::memory_order_relaxed);
  
  bool isGlobal = state.hasHint(transaction::Hints::Hint::GLOBAL);
  if (!isGlobal && !keepTransactionData(state)) {
    return;
  }

  size_t bucket = getBucket(state.id());
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);
     
  WRITE_LOCKER(writeLocker, _transactions[bucket]._lock);
  
  if (isGlobal) {
    data->_state = &state;
    data->_expires = TRI_microtime() + defaultTTL;
  }
  
  // insert into currently running list of transactions
  _transactions[bucket]._activeTransactions.emplace(state.id(), std::move(data));
}

// unregisters a transaction
void TransactionManager::unregisterTransaction(TRI_voc_tid_t transactionId,
                                               bool markAsFailed) {
  _nrRunning.fetch_sub(1, std::memory_order_relaxed);
  
  size_t bucket = getBucket(transactionId);
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);
    
  WRITE_LOCKER(writeLocker, _transactions[bucket]._lock);

  _transactions[bucket]._activeTransactions.erase(transactionId);

  if (markAsFailed) {
    _transactions[bucket]._failedTransactions.emplace(transactionId);
  }
}

// return the set of failed transactions
std::unordered_set<TRI_voc_tid_t> TransactionManager::getFailedTransactions() const {
  std::unordered_set<TRI_voc_tid_t> failedTransactions;

  {
    WRITE_LOCKER(allTransactionsLocker, _allTransactionsLock);

    for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
      READ_LOCKER(locker, _transactions[bucket]._lock);

      for (auto const& it : _transactions[bucket]._failedTransactions) {
        failedTransactions.emplace(it);
      }
    }
  }

  return failedTransactions;
}

void TransactionManager::iterateActiveTransactions(std::function<void(TRI_voc_tid_t, TransactionData const*)> const& callback) {
  WRITE_LOCKER(allTransactionsLocker, _allTransactionsLock);

  // iterate over all active transactions 
  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    READ_LOCKER(locker, _transactions[bucket]._lock);

    for (auto const& it : _transactions[bucket]._activeTransactions) {
      callback(it.first, it.second.get());
    }
  }
}

uint64_t TransactionManager::getActiveTransactionCount() {
  return _nrRunning.load(std::memory_order_relaxed);
  /*WRITE_LOCKER(allTransactionsLocker, _allTransactionsLock);
  
  uint64_t count = 0;
  // iterate over all active transactions
  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    READ_LOCKER(locker, _transactions[bucket]._lock);
    count += _transactions[bucket]._activeTransactions.size();
  }
  return count;*/
}

TransactionState* TransactionManager::leaseTransaction(TRI_voc_tid_t transactionId) const {
  size_t bucket = getBucket(transactionId);
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);
  
  READ_LOCKER(writeLocker, _transactions[bucket]._lock);
  
  auto const& it = _transactions[bucket]._activeTransactions.find(transactionId);
  if (it != _transactions[bucket]._activeTransactions.end()) {
    if (it->second->_state != nullptr) {
      TRI_ASSERT(it->second->_state->hasHint(transaction::Hints::Hint::GLOBAL));
      if (it->second->_state->isEmbeddedTransaction()) {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_TRANSACTION_INTERNAL,
                                       "Concurrent use of transaction");
      }
      it->second->_expires = defaultTTL + TRI_microtime();
      it->second->_state->increaseNesting();
      return it->second->_state;
    }
  }
  return nullptr;
}

void TransactionManager::garbageCollect() {
  READ_LOCKER(allTransactionsLocker, _allTransactionsLock);
  
  for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
    WRITE_LOCKER(locker, _transactions[bucket]._lock);
    
    double now = TRI_microtime();
    for (auto const& it : _transactions[bucket]._activeTransactions) {
      // we only concern ourselves with global transactions
      if (it.second->_state != nullptr) {
        TRI_ASSERT(it.second->_state->hasHint(transaction::Hints::Hint::GLOBAL));
        if (it.second->_state->isTopLevelTransaction()) {
          if (it.second->_expires > now) {
#warning TODO cleanup
          }
        } else {
          it.second->_expires = defaultTTL + TRI_microtime();
        }
      }
    }
  }
}
