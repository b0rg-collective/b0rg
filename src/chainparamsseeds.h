#ifndef BITCOIN_CHAINPARAMSSEEDS_H
#define BITCOIN_CHAINPARAMSSEEDS_H
/**
 * Assimilation Coin ships no fixed peers. Connect with -addnode.
 * The arrays stay so existing call sites compile; they are not installed.
 */
static const uint8_t chainparams_seed_main[] = {0};
static const uint8_t chainparams_seed_test[] = {0};
#endif
