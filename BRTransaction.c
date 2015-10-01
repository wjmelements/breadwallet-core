//
//  BRTransaction.c
//
//  Created by Aaron Voisine on 8/31/15.
//  Copyright (c) 2015 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "BRTransaction.h"
#include "BRKey.h"
#include "BRAddress.h"
#include <time.h>
#include <unistd.h>

#define TX_VERSION    0x00000001u
#define TX_LOCKTIME   0x00000000u
#define TXIN_SEQUENCE UINT32_MAX
#define SIGHASH_ALL   0x00000001u

// returns a random number less than upperBound, for non-cryptographic use only
uint32_t BRRand(uint32_t upperBound)
{
    static int first = ! 0;
    uint32_t r;
    
    if (first) srand(((unsigned)time(NULL) ^ getpid())*0x01000193); // seed = (time xor pid)*FNV_PRIME
    first = 0;
    if (upperBound == 0 || upperBound > BR_RAND_MAX) upperBound = BR_RAND_MAX;
    
    do { // to avoid modulo bias, find a rand value not less than 0x100000000 % upperBound
        r = rand();
    } while (r < ((0xffffffff - upperBound*2) + 1) % upperBound); // ((0x100000000 - x*2) % x) == (0x100000000 % x)

    return r % upperBound;
}

static size_t BRTransactionData(BRTransaction *tx, uint8_t *data, size_t len, size_t subscriptIdx)
{
    size_t off = 0;

    if (data && off + sizeof(uint32_t) <= len) *(uint32_t *)&data[off] = le32(tx->version);
    off += sizeof(uint32_t);
    off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), tx->inCount);

    for (size_t i = 0; i < tx->inCount; i++) {
        BRTxInput *in = &tx->inputs[i];
        
        if (data && off + sizeof(UInt256) <= len) memcpy(&data[off], &in->txHash, sizeof(UInt256));
        off += sizeof(UInt256);
        if (data && off + sizeof(uint32_t) <= len) *(uint32_t *)&data[off] = le32(in->index);
        off += sizeof(uint32_t);

        if (in->signature && in->sigLen > 0 && subscriptIdx < tx->inCount) {
            off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), in->sigLen);
            if (off + in->sigLen <= len) memcpy(&data[off], in->signature, in->sigLen);
            off += in->sigLen;
        }
        else if (subscriptIdx == i && in->script && in->scriptLen > 0) {
            //TODO: to fully match the reference implementation, OP_CODESEPARATOR related checksig logic should go here
            off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), in->scriptLen);
            if (off + in->scriptLen <= len) memcpy(&data[off], in->script, in->scriptLen);
            off += in->scriptLen;

        }
        else off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), 0);
        
        if (data && off + sizeof(uint32_t) <= len) *(uint32_t *)&data[off] = le32(in->sequence);
        off += sizeof(uint32_t);
    }
    
    off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), tx->outCount);
    
    for (size_t i = 0; i < tx->outCount; i++) {
        BRTxOutput *out = &tx->outputs[i];
        
        if (data && off + sizeof(uint64_t) <= len) *(uint64_t *)&data[off] = le64(out->amount);
        off += sizeof(uint64_t);
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= len ? len - off : 0), out->scriptLen);
        if (off + out->scriptLen <= len) memcpy(&data[off], out->script, out->scriptLen);
        off += out->scriptLen;
    }
    
    if (data && off + sizeof(uint32_t) <= len) *(uint32_t *)&data[off] = le32(tx->lockTime);
    off += sizeof(uint32_t);

    if (subscriptIdx < tx->inCount) {
        if (data && off + sizeof(uint32_t) <= len) *(uint32_t *)&data[off] = le32(SIGHASH_ALL);
        off += sizeof(uint32_t);
    }

    return (! data || off <= len) ? off : 0;
}

// returns a newly allocated empty transaction that must be freed by calling BRTransactionFree()
BRTransaction *BRTransactionNew()
{
    BRTransaction *tx = calloc(1, sizeof(BRTransaction));

    array_new(tx->inputs, 1);
    array_new(tx->outputs, 2);
    return tx;
}

// buf must contain a serialized tx, result must be freed by calling BRTransactionFree()
BRTransaction *BRTransactionDeserialize(const uint8_t *buf, size_t len)
{
    return NULL;
}

// returns number of bytes written to buf, or total len needed if buf is NULL
size_t BRTransactionSerialize(BRTransaction *tx, uint8_t *buf, size_t len)
{
    return BRTransactionData(tx, buf, len, SIZE_MAX);
}

// adds an input to tx
void BRTransactionAddInput(BRTransaction *tx, BRTxInput *input)
{
    array_add(tx->inputs, *input);
    tx->inCount = array_count(tx->inputs);
}

// adds an output to tx
void BRTransactionAddOutput(BRTransaction *tx, BRTxOutput *output)
{
    array_add(tx->outputs, *output);
    tx->outCount = array_count(tx->outputs);
}

// shuffles order of tx outputs
void BRTransactionShuffleOutputs(BRTransaction *tx)
{
    for (uint32_t i = 0; i + 1 < tx->outCount; i++) { // fischer-yates shuffle
        uint32_t j = i + BRRand((uint32_t)tx->outCount - i);
        BRTxOutput t;
        
        if (j == i) continue;
        t = tx->outputs[i];
        tx->outputs[i] = tx->outputs[j];
        tx->outputs[j] = t;
    }
}

// size in bytes if signed, or estimated size assuming compact pubkey sigs
size_t BRTransactionSize(BRTransaction *tx)
{
    static const size_t sigSize = 149; // signature size using a compact pubkey
//    static const size_t sigSize = 181; // signature size using a non-compact pubkey
    
    if (! UInt256IsZero(tx->txHash)) return BRTransactionData(tx, NULL, 0, SIZE_MAX);
    return 8 + BRVarIntSize(tx->inCount) + tx->inCount*sigSize + BRVarIntSize(tx->outCount) + tx->outCount*34;
}

// minimum transaction fee needed for tx to relay across the bitcoin network
inline uint64_t BRTransactionStandardFee(BRTransaction *tx)
{
    return ((BRTransactionSize(tx) + 999)/1000)*TX_FEE_PER_KB;
}

// checks if all signatures exist, but does not verify them
int BRTransactionIsSigned(BRTransaction *tx)
{
    for (size_t i = 0; i < tx->inCount; i++) {
        if (! tx->inputs[i].signature || tx->inputs[i].sigLen == 0) return 0;
    }

    return ! 0;
}

// adds signatures to any inputs with NULL signatures that can be signed with any privKeys, returns true if tx is signed
int BRTransactionSign(BRTransaction *tx, const char *privKeys[], size_t count)
{
    BRKey keys[count];
    BRAddress addrs[count], address;
    size_t i, j, len;
    
    for (i = 0, j = 0; i < count; i++) {
        if (BRKeySetPrivKey(&keys[j], privKeys[i]) && BRKeyAddress(&keys[j], addrs[j].s, sizeof(addrs[j])) > 0) j++;
    }
    
    count = j;

    for (i = 0, j = 0; i < tx->inCount; i++) {
        BRTxInput *in = &tx->inputs[i];
        
        if (! BRAddressFromScriptPubKey(address.s, sizeof(address), in->script, in->scriptLen)) continue;
        while (j < count && ! BRAddressEq(&addrs[j], &address)) j++;
        if (j >= count) continue;

        const uint8_t *elems[BRScriptElements(NULL, 0, in->script, in->scriptLen)];
        uint8_t data[BRTransactionData(tx, NULL, 0, i)], sig[OP_PUSHDATA1 - 1], pubKey[65];
        UInt256 hash = UINT256_ZERO;
        
        len = BRTransactionData(tx, data, sizeof(data), i);
        BRSHA256_2(&hash, data, len);
        len = BRKeySign(&keys[j], sig, sizeof(sig) - 1, hash);
        if (len == 0) continue;
        sig[len++] = SIGHASH_ALL;
        if (in->signature) array_free(in->signature);
        array_new(in->signature, 1 + len + 34);
        array_add(in->signature, len);
        array_add_array(in->signature, sig, len);
        len = BRScriptElements(elems, sizeof(elems)/sizeof(*elems), in->script, in->scriptLen);
        
        if (len >= 2 && *elems[len - 2] == OP_EQUALVERIFY) { // pay-to-pubkey-hash scriptSig
            len = BRKeyPubKey(&keys[j], pubKey, sizeof(pubKey));
            array_add(in->signature, len);
            array_add_array(in->signature, pubKey, len);
        }
        
        in->sigLen = array_count(in->signature);
    }
    
    for (i = 0; i < count; i++) BRKeyClean(&keys[i]);
    if (! BRTransactionIsSigned(tx)) return 0;
    
    uint8_t data[BRTransactionData(tx, NULL, 0, SIZE_MAX)];
    
    len = BRTransactionData(tx, data, sizeof(data), SIZE_MAX);
    BRSHA256_2(&tx->txHash, data, len);
    return ! 0;
}

// frees memory allocated for tx
void BRTransactionFree(BRTransaction *tx)
{
    for (size_t i = 0; i < tx->inCount; i++) {
        if (tx->inputs[i].script) array_free(tx->inputs[i].script);
        if (tx->inputs[i].signature) array_free(tx->inputs[i].signature);
    }

    for (size_t i = 0; i < tx->outCount; i++) {
        if (tx->outputs[i].script) array_free(tx->outputs[i].script);
    }

    array_free(tx->outputs);
    array_free(tx->inputs);
    free(tx);
}