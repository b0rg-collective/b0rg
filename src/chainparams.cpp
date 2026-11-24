// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2026 The Assimilation Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <arith_uint256.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>

#include <assert.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint64_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nTimePad = 0;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Genesis coinbase is unspendable (it is never connected into the UTXO set)
 * and its value is 0. There is no premine.
 *
 * Set B0RG_MINE_GENESIS=1 to search for a valid nonce and print the results.
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint64_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "Nov 24 2026 \xE2\x80\x94 Mint your drone army. Assimilate cyberspace. Resistance is futile.";
    const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

static bool MineGenesisRequested(const char* label)
{
    const char* env = std::getenv("B0RG_MINE_GENESIS");
    return env != nullptr && std::strcmp(env, label) == 0;
}

static bool MineGenesisAny()
{
    const char* env = std::getenv("B0RG_MINE_GENESIS");
    return env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0;
}

static void MineGenesis(CBlock& genesis, const char* label)
{
    if (!MineGenesisRequested(label))
        return;

    arith_uint256 target;
    bool neg = false, over = false;
    target.SetCompact(genesis.nBits, &neg, &over);
    genesis.nTimePad = 0;
    genesis.nNonce = 0;
    while (UintToArith256(genesis.GetPoWHash()) > target) {
        ++genesis.nNonce;
        if ((genesis.nNonce & 0xFFFFF) == 0) {
            std::fprintf(stderr, "%s nonce %llu\n", label, (unsigned long long)genesis.nNonce);
        }
    }
    std::fprintf(stderr,
        "%s GENESIS FOUND\n"
        "  hash   %s\n"
        "  pow    %s\n"
        "  merkle %s\n"
        "  nonce  %llu\n"
        "  bits   0x%08x\n"
        "  time   %u\n",
        label,
        genesis.GetHash().ToString().c_str(),
        genesis.GetPoWHash().ToString().c_str(),
        genesis.hashMerkleRoot.ToString().c_str(),
        (unsigned long long)genesis.nNonce,
        genesis.nBits,
        genesis.nTime);
    std::fflush(stderr);
    std::exit(0);
}

static void ApplyCommonConsensus(Consensus::Params& consensus, bool testnet_min_diff, bool no_retarget)
{
    consensus.nSubsidyHalvingInterval = 2000000;
    consensus.BIP16Height = 0;
    consensus.BIP34Height = 1;
    consensus.BIP34Hash = uint256();
    consensus.BIP65Height = 1;
    consensus.BIP66Height = 1;
    consensus.CSVHeight = 1;
    consensus.SegwitHeight = 1; // active from the first block after genesis
    consensus.MinBIP9WarningHeight = 1;
    consensus.nPowTargetTimespan = 30 * 60;
    consensus.nPowTargetSpacing = 30;
    consensus.fPowAllowMinDifficultyBlocks = testnet_min_diff;
    consensus.fPowNoRetargeting = no_retarget;
    consensus.nRuleChangeActivationThreshold = 45;
    consensus.nMinerConfirmationWindow = 60;

    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
    consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

    consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
    consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
    consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

    // MWEB is active for every block that has a parent (height >= 1).
    // IsMWEBEnabled(nullptr) stays false so the first HogEx has no parent output.
    consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].bit = 4;
    consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
    consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

    consensus.nMinimumChainWork = uint256{};
    consensus.defaultAssumeValid = uint256{};
    consensus.signet_blocks = false;
    consensus.signet_challenge.clear();
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        ApplyCommonConsensus(consensus, false, false);
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        pchMessageStart[0] = 0xb0;
        pchMessageStart[1] = 0x46;
        pchMessageStart[2] = 0x0b;
        pchMessageStart[3] = 0x26;
        nDefaultPort = 19333;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1795478400, 1104784, 0x1e0ffff0, 1, 0);
        MineGenesis(genesis, "main");
        consensus.hashGenesisBlock = genesis.GetHash();
        if (!MineGenesisAny()) {
            assert(consensus.hashGenesisBlock == uint256S("0x7e3e2d4ad8c78b9f2cdb53657a48d9474049a10393fbfe2678df819c07657799"));
            assert(genesis.hashMerkleRoot == uint256S("0xdfc5b2693cd26d23c70b7d711658cd86c11b5b166664c66ff69f0acac611d06b"));
        }

        vFixedSeeds.clear();
        vSeeds.clear();
        (void)chainparams_seed_main;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 25); // B...
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 30);  // D...
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 30); // D...
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 153);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0xB0, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0xB0, 0xAD, 0xE4};

        bech32_hrp = "borg";
        mweb_hrp = "borgmweb";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, uint256S("0x7e3e2d4ad8c78b9f2cdb53657a48d9474049a10393fbfe2678df819c07657799")},
            }
        };
        chainTxData = ChainTxData{0, 0, 0};
    }
};

/**
 * Testnet
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        ApplyCommonConsensus(consensus, true, false);
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        pchMessageStart[0] = 0xb0;
        pchMessageStart[1] = 0x46;
        pchMessageStart[2] = 0x0b;
        pchMessageStart[3] = 0x54;
        nDefaultPort = 29333;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 1;
        m_assumed_chain_state_size = 1;

        genesis = CreateGenesisBlock(1795478401, 1635351, 0x1e0ffff0, 1, 0);
        MineGenesis(genesis, "test");
        consensus.hashGenesisBlock = genesis.GetHash();
        if (!MineGenesisAny()) {
            assert(consensus.hashGenesisBlock == uint256S("0x9bff2bdbde6b9e17bdc4f50af740076eb71921f481949597ceaa32dc96c5e386"));
            assert(genesis.hashMerkleRoot == uint256S("0xdfc5b2693cd26d23c70b7d711658cd86c11b5b166664c66ff69f0acac611d06b"));
        }

        vFixedSeeds.clear();
        vSeeds.clear();
        (void)chainparams_seed_test;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 120);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 125);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 125);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 248);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0xB0, 0x35, 0x87};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0xB0, 0x35, 0x83};

        bech32_hrp = "tborg";
        mweb_hrp = "tborgmweb";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;

        checkpointData = {
            {
                {0, uint256S("0x9bff2bdbde6b9e17bdc4f50af740076eb71921f481949597ceaa32dc96c5e386")},
            }
        };
        chainTxData = ChainTxData{0, 0, 0};
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID = CBaseChainParams::REGTEST;
        ApplyCommonConsensus(consensus, true, true);
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        pchMessageStart[0] = 0xb0;
        pchMessageStart[1] = 0x46;
        pchMessageStart[2] = 0x0b;
        pchMessageStart[3] = 0x52;
        nDefaultPort = 19433;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        genesis = CreateGenesisBlock(1795478402, 2, 0x207fffff, 1, 0);
        MineGenesis(genesis, "regtest");
        consensus.hashGenesisBlock = genesis.GetHash();
        if (!MineGenesisAny()) {
            assert(consensus.hashGenesisBlock == uint256S("0x4a9e01c53b9b548d4ebdf77ed0b7b887667ae2da2bc52c3255234df7ae040a29"));
            assert(genesis.hashMerkleRoot == uint256S("0xdfc5b2693cd26d23c70b7d711658cd86c11b5b166664c66ff69f0acac611d06b"));
        }

        vFixedSeeds.clear();
        vSeeds.clear();

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;
        m_is_mockable_chain = true;

        checkpointData = {
            {
                {0, uint256S("0x4a9e01c53b9b548d4ebdf77ed0b7b887667ae2da2bc52c3255234df7ae040a29")},
            }
        };
        chainTxData = ChainTxData{0, 0, 0};

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 120);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 125);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 125);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 248);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0xB0, 0x35, 0x87};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0xB0, 0x35, 0x83};

        bech32_hrp = "rborg";
        mweb_hrp = "rborgmweb";
    }

    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nStartHeight, int64_t nTimeoutHeight)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
        consensus.vDeployments[d].nStartHeight = nStartHeight;
        consensus.vDeployments[d].nTimeoutHeight = nTimeoutHeight;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (args.IsArgSet("-segwitheight")) {
        int64_t height = args.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() < 3 || 5 < vDeploymentParams.size()) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end[:heightstart:heightend]");
        }
        int64_t nStartTime, nTimeout, nStartHeight = 0, nTimeoutHeight = 0;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        if (vDeploymentParams.size() > 3 && !ParseInt64(vDeploymentParams[3], &nStartHeight)) {
            throw std::runtime_error(strprintf("Invalid nStartHeight (%s)", vDeploymentParams[3]));
        }
        if (vDeploymentParams.size() > 4 && !ParseInt64(vDeploymentParams[4], &nTimeoutHeight)) {
            throw std::runtime_error(strprintf("Invalid nTimeoutHeight (%s)", vDeploymentParams[4]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout, nStartHeight, nTimeoutHeight);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld, start_height=%d, timeout_height=%d\n", vDeploymentParams[0], nStartTime, nTimeout, nStartHeight, nTimeoutHeight);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN) {
        return std::unique_ptr<CChainParams>(new CMainParams());
    } else if (chain == CBaseChainParams::TESTNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::SIGNET) {
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    } else if (chain == CBaseChainParams::REGTEST) {
        return std::unique_ptr<CChainParams>(new CRegTestParams(args));
    }
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}
