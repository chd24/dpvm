/* dpvm: words; T15.404-T20.174; $DVS:time$ */

#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "init.h"
#include "name.h"
#include "object.h"

#define N_WORDS		54413

const char base58Alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static uint64_t word2code(const char *word) {
	uint64_t code = 0;
	int i;
	for (i = 0; i < DPVM_WORD_SIZE_MAX; ++i) {
		code <<= 5; 
		code |= *word & 31;
		if (*word) word++;
	}
	return code;
}

static void code2word(uint64_t code, char word[DPVM_WORD_SIZE_MAX + 1]) {
	int i;
	for (i = 0; i < DPVM_WORD_SIZE_MAX; ++i) {
		int c = code >> ((DPVM_WORD_SIZE_MAX - 1 - i) * 5) & 31;
		if (c) word[i] = c + 'a' - 1;
		else break;
	}
	word[i] = 0;
}

static int find_code(int64_t *codes, int64_t code, int min, int max) {
	int mid = (min + max) / 2;
	if (min > max) return -1;
	     if (codes[mid] > code) return find_code(codes, code, min, mid - 1);
	else if (codes[mid] < code) return find_code(codes, code, mid + 1, max);
	else return mid;
}

int dpvm_name_init(struct dpvm *dpvm) {
	const struct dpvm_hash words_hash = {{
		0x50e67a30b70ab897, 0xeee5557038ee169c,
		0x1f192cba4b2536a0, 0x5b05ba913eae0d6f
	}};
	struct dpvm_hash *hash;
	struct dpvm_object *type;
	char word[DPVM_WORD_SIZE_MAX + 1];
	uint64_t code, prev;
	FILE *f;
	int i, err;

	dpvm->words = 0;
	dpvm->words = dpvm_hash2object(dpvm, 0, &words_hash);
	if (dpvm->words)
		return 0;

	type = dpvm_create_type(0, 0, N_WORDS, 0, 0, 0, N_WORDS, 0, 0, dpvm->any, dpvm->any, dpvm->any, dpvm->any, 0, 0);
	if (!type) {
		printf("Error: can't create dictionary type\n");
		return -3;
	}

	dpvm->words = dpvm_create_object(0, type, 0, N_WORDS, 0, 0);
	if (!dpvm->words) {
		printf("Error: can't create dictionary object\n");
		return -4;
	}
	dpvm_unlink_object(0, type);

	f = fopen("words.txt", "r");
	if (!f) {
		printf("Error: can't find file words.txt\n");
		return -5;
	}

	for (i = 0; i < N_WORDS; ++i) {
		fscanf(f, "%s", word);
		code = word2code(word);
		if (i && code <= prev) {
			printf("Error: unsorted dictionary in words.txt at word %s\n",
				word);
			return -6;
		}
		dpvm->words->ints[i] = code;
		prev = code;
	}
	
	err = dpvm_cache_add(dpvm, 0, &dpvm->words);
	if (err)
		return err;
	
	hash = dpvm_object_hash(dpvm->words, -1ull);
	if (!hash)
		return -7;
	if (memcmp(&words_hash, hash, DPVM_HASH_SIZE)) {
		printf("\n!!!Error: hash of words object mismatches, "
			"new hash: [0x%016lx 0x%016lx 0x%016lx 0x%016lx].\n",
			(unsigned long)hash->hash[0], (unsigned long)hash->hash[1], 
			(unsigned long)hash->hash[2], (unsigned long)hash->hash[3]);
		return -8;
	}
	
	return err;
}

int dpvm_short_hash2name(struct dpvm *dpvm, uint64_t short_hash, char name[DPVM_NAME_SIZE_MAX + 1]) {
	char *ptr = name;
	int i;
	short_hash &= DPVM_SHORT_HASH_MASK;
	if (!dpvm->words) {
		sprintf(name, "short_hash(%012lx)", (unsigned long)short_hash);
		return -1;
	}
	for (i = 0; i < 3; ++i) {
		uint64_t code = dpvm->words->ints[short_hash % N_WORDS];
		short_hash /= N_WORDS;
		code2word(code, ptr);
		while (*ptr) ptr++;
		if (i < 2) *ptr++ = '_';
	}
	return 0;
}

static int64_t dpvm_short_name2short_hash(struct dpvm *dpvm, const char *name) {
	char word[DPVM_WORD_SIZE_MAX + 1];
	uint64_t code, short_hash = 0;
	int i, j, ind[3];
	for (i = 0; i < 3; ++i) {
		j = 0;
		while (*name >= 'a' && *name <= 'z' && j < DPVM_WORD_SIZE_MAX) {
			word[j++] = *name++;
		}
		word[j] = 0;
		if (*name++ != (i == 2 ? 0 : '_'))
			return -1;
		code = word2code(word);
		ind[i] = find_code(dpvm->words->ints, code, 0, N_WORDS - 1);
		if (ind[i] < 0)
			return -1;
	}
	for (i = 0; i < 3; ++i) {
		short_hash *= N_WORDS;
		short_hash += ind[2 - i];
	}
	if (short_hash & ~DPVM_SHORT_HASH_MASK)
		return -1;
	return short_hash;
}

int64_t dpvm_name2hash(struct dpvm *dpvm, const char *name, struct dpvm_hash *hash) {
	if (strlen(name) == 47 && name[11] == '_' && name[23] == '_' && name[35] == '_') {
		int i, j, k;
		for (i = 0; i < 4; i++) {
			uint64_t h = 0;
			for (j = 12 * i; j < 12 * i + 11; j++) {
				h *= 58;
				for (k = (j == 12 * i ? 9 : 0); name[j] != base58Alphabet[k]; k++, h++)
					if (k == 57) return -1;
			}
			hash->hash[i] = h;
		}
		return (hash->hash[0] & DPVM_SHORT_HASH_MASK) | (DPVM_SHORT_HASH_MASK + 1);
	} else
		return dpvm_short_name2short_hash(dpvm, name);
}
