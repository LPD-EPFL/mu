#pragma once

#include <algorithm>
#include <map>
#include <vector>
#include "message-identifier.hpp"

#include <dory/extern/ibverbs.hpp>

namespace dory {
class FailedMajorityTracker {
 public:
  FailedMajorityTracker() {}

  FailedMajorityTracker(quorum::Kind kind, std::vector<int>& remote_ids,
                        int tolerated_failures)
      : kind{kind}, tolerated_failures{tolerated_failures}, track_id{0} {
    auto max_elem = Identifiers::maxID(remote_ids);
    failures.resize(max_elem + 1);
  }

  void startTracking(uint64_t id) {
    if (track_id != id) {
      track_id = id;
      failed = 0;
      std::fill(failures.begin(), failures.end(), false);
    }
  }

  bool isUnrecoverableTrackedResponse(std::vector<struct ibv_wc>& entries) {
    for (auto const& entry : entries) {
      if (entry.status != IBV_WC_SUCCESS) {
        auto [k, pid, seq] = quorum::unpackAll<uint64_t, uint64_t>(entry.wr_id);

        if (k == kind && seq == track_id && !failures[pid]) {
          failures[pid] = true;
          failed += 1;
        } else {
          std::cout << "Found unrelated remnants in the polled responses"
                    << std::endl;
        }
      }

      if (failed > tolerated_failures) {
        return true;
      }
    }

    return false;
  }

 private:
  quorum::Kind kind;
  int tolerated_failures;

  std::vector<uint64_t> failures;
  uint64_t track_id;
  int failed;
};
}  // namespace dory

namespace dory {
class MaybeError {
 public:
  enum ErrorType {
    GenericError,
    NoError,
    UpdateProposalError,
    ReadLogMajorityError,
    WriteLogMajorityError,

    ReadProposalMajorityError,
    WriteProposalMajorityError,
    CatchProposalRetryError,

    ReadFUOMajorityError,
    WriteFUODiffMajorityError,

    LeaderSwitchRequestError
  };

  static const char* type_str(ErrorType e) {
    const std::map<ErrorType, const char*> MyEnumStrings{
        {ErrorType::GenericError, "ErrorType::GenericError"},
        {ErrorType::NoError, "ErrorType::NoError"},
        {ErrorType::UpdateProposalError, "ErrorType::UpdateProposalError"},
        {ErrorType::ReadLogMajorityError, "ErrorType::ReadLogMajorityError"},
        {ErrorType::WriteLogMajorityError, "ErrorType::WriteLogMajorityError"},

        {ErrorType::ReadProposalMajorityError,
         "ErrorType::ReadProposalMajorityError"},
        {ErrorType::WriteProposalMajorityError,
         "ErrorType::WriteProposalMajorityError"},
        {ErrorType::CatchProposalRetryError,
         "ErrorType::CatchProposalRetryError"},

        {ErrorType::ReadFUOMajorityError, "ErrorType::ReadFUOMajorityError"},
        {ErrorType::WriteFUODiffMajorityError,
         "ErrorType::WriteFUODiffMajorityError"},

        {ErrorType::LeaderSwitchRequestError,
         "ErrorType::LeaderSwitchRequestError"},
    };
    auto it = MyEnumStrings.find(e);
    return it == MyEnumStrings.end() ? "Out of range" : it->second;
  }

  virtual inline bool ok() { return false; }
  virtual inline ErrorType type() { return MaybeError::GenericError; }

  virtual ~MaybeError() {}

  static const MaybeError::ErrorType value = MaybeError::GenericError;
};

class NoError : public MaybeError {
 public:
  NoError() {}

  inline bool ok() override { return true; }
  inline ErrorType type() override { return MaybeError::NoError; }

  static const MaybeError::ErrorType value = MaybeError::NoError;
};

class UpdateProposalError : public MaybeError {
 public:
  UpdateProposalError(uint64_t proposal_nr) : proposal_nr{proposal_nr} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override { return MaybeError::UpdateProposalError; }
  inline uint64_t proposal() { return proposal_nr; }

  static const MaybeError::ErrorType value = MaybeError::UpdateProposalError;

 private:
  uint64_t proposal_nr;
};

class ReadLogMajorityError : public MaybeError {
 public:
  ReadLogMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override { return MaybeError::ReadLogMajorityError; }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value = MaybeError::ReadLogMajorityError;

 private:
  uint64_t req_id;
};

class WriteLogMajorityError : public MaybeError {
 public:
  WriteLogMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override { return MaybeError::WriteLogMajorityError; }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value = MaybeError::WriteLogMajorityError;

 private:
  uint64_t req_id;
};

class ReadProposalMajorityError : public MaybeError {
 public:
  ReadProposalMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override {
    return MaybeError::ReadProposalMajorityError;
  }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value =
      MaybeError::ReadProposalMajorityError;

 private:
  uint64_t req_id;
};

class WriteProposalMajorityError : public MaybeError {
 public:
  WriteProposalMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override {
    return MaybeError::WriteProposalMajorityError;
  }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value =
      MaybeError::WriteProposalMajorityError;

 private:
  uint64_t req_id;
};

class CatchProposalRetryError : public MaybeError {
 public:
  CatchProposalRetryError(uint64_t proposal_nr) : proposal_nr{proposal_nr} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override {
    return MaybeError::CatchProposalRetryError;
  }
  inline uint64_t proposal() { return proposal_nr; }

  static const MaybeError::ErrorType value =
      MaybeError::CatchProposalRetryError;

 private:
  uint64_t proposal_nr;
};

class LeaderSwitchRequestError : public MaybeError {
 public:
  LeaderSwitchRequestError(uint64_t req_nr) : req_nr{req_nr} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override {
    return MaybeError::LeaderSwitchRequestError;
  }
  inline uint64_t proposal() { return req_nr; }

  static const MaybeError::ErrorType value =
      MaybeError::LeaderSwitchRequestError;

 private:
  uint64_t req_nr;
};

class ReadFUOMajorityError : public MaybeError {
 public:
  ReadFUOMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override { return MaybeError::ReadFUOMajorityError; }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value = MaybeError::ReadFUOMajorityError;

 private:
  uint64_t req_id;
};

class WriteFUODiffMajorityError : public MaybeError {
 public:
  WriteFUODiffMajorityError(uint64_t req_id) : req_id{req_id} {}

  inline bool ok() override { return false; }
  inline ErrorType type() override {
    return MaybeError::WriteFUODiffMajorityError;
  }
  inline uint64_t req() { return req_id; }

  static const MaybeError::ErrorType value =
      MaybeError::WriteFUODiffMajorityError;

 private:
  uint64_t req_id;
};

}  // namespace dory