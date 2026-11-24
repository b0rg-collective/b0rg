// Copyright (c) 2014-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <signet.h>
#include <validation.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(validation_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainParams->GetConsensus();
    BOOST_CHECK_EQUAL(consensus.nSubsidyHalvingInterval, 2000000);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, consensus), CAmount{0});
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, consensus), B0RG_BASE_SUBSIDY + B0RG_BLOCK1_REMAINDER);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(2000000, consensus), B0RG_BASE_SUBSIDY);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(2000001, consensus), B0RG_BASE_SUBSIDY / 2);
    BOOST_CHECK(MoneyRange(GetBlockSubsidy(1, consensus)));
}

BOOST_AUTO_TEST_SUITE_END()
