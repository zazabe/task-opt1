// Extract from basisu_transcoder.cpp
// Copyright (C) 2019-2021 Binomial LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <array>
#include <tuple>
#include <any>
#include <bits/stdc++.h>
#include "kdtree.hpp"

using namespace std;

//************************** Helpers and Boilerplate **************************/

#include "basisu_headers.h"

/**
 * Helper to return the current time in milliseconds.
 */
static unsigned millis()
{
	return static_cast<unsigned>((clock() * 1000LL) / CLOCKS_PER_SEC);
}

/**
 * Prebuilt table with known results.
 */
static const etc1_to_dxt1_56_solution known[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES] = {
#include "basisu_transcoder_tables_dxt1_6.inc"
};

/**
 * Helper to compare two tables to see if they match.
 */
static bool verifyTable(const etc1_to_dxt1_56_solution *a, const etc1_to_dxt1_56_solution *b)
{
	for (unsigned n = 0; n < 32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES; n++)
	{
		if (a->m_hi != b->m_hi || a->m_lo != b->m_lo || a->m_err != b->m_err)
		{
			printf("hi  %d vs %d ", a->m_hi, b->m_hi);
			printf("lo  %d vs %d ", a->m_lo, b->m_lo);
			printf("err %d vs %d ", a->m_err, b->m_err);
			printf("Failed with n = %d\n", n);
			return false;
		}
		a++;
		b++;
	}
	return true;
}

//************************ Optimisation Task Goes Here ************************/

struct LoHi
{
	uint32_t lo;
	uint32_t hi;
};

/**
 * Results stored here.
 */
static etc1_to_dxt1_56_solution result[32 * 8 * NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS * NUM_ETC1_TO_DXT1_SELECTOR_RANGES];

Kdtree::KdTree prepare_dxt1_color_tree()
{
	Kdtree::KdNodeVector nodes;
	for (uint32_t hi = 0; hi <= 63; hi++)
	{
		for (uint32_t lo = 0; lo <= 63; lo++)
		{
			std::vector<double> point(4);
			LoHi *lohi = new LoHi({lo : lo, hi : hi});
			void *data = static_cast<void *>(lohi);
			point[0] = (lo << 2) | (lo >> 4);
			point[3] = (hi << 2) | (hi >> 4);
			point[1] = (point[0] * 2 + point[3]) / 3;
			point[2] = (point[3] * 2 + point[0]) / 3;
			nodes.push_back(Kdtree::KdNode(point, data));
		}
	}
	return Kdtree::KdTree(&nodes, 0);
}

/**
 * Function to optimise.
 */
static void create_etc1_to_dxt1_6_conversion_table()
{
	uint32_t n = 0;

	int inten_max = 8;
	uint32_t g_max = 32;
	Kdtree::KdTree dxt1_tree = prepare_dxt1_color_tree();

	for (int inten = 0; inten < inten_max; inten++)
	{

		for (uint32_t g = 0; g < g_max; g++)
		{
			color32 block_colors[4];
			decoder_etc_block::get_diff_subblock_colors(block_colors, decoder_etc_block::pack_color5(color32(g, g, g, 255), false), inten);

			for (uint32_t sr = 0; sr < NUM_ETC1_TO_DXT1_SELECTOR_RANGES; sr++)
			{
				const uint32_t low_selector = g_etc1_to_dxt1_selector_ranges[sr].m_low;
				const uint32_t high_selector = g_etc1_to_dxt1_selector_ranges[sr].m_high;

				for (uint32_t m = 0; m < NUM_ETC1_TO_DXT1_SELECTOR_MAPPINGS; m++)
				{

					uint32_t best_err = UINT32_MAX;
					uint32_t best_lo;
					uint32_t best_hi;

					std::vector<double> lookup_point(4);
					lookup_point[0] = (double)block_colors[0].g;
					lookup_point[1] = (double)block_colors[1].g;
					lookup_point[2] = (double)block_colors[2].g;
					lookup_point[3] = (double)block_colors[3].g;

					Kdtree::KdNodeVector neighbors;
					dxt1_tree.k_nearest_neighbors(lookup_point, 50, &neighbors);

					for (uint32_t i = 0; i < neighbors.size(); ++i)
					{
						uint32_t total_err = 0;

						for (uint32_t s = low_selector; s <= high_selector; s++)
						{
							int err = block_colors[s].g - (uint8_t)neighbors[i].point[g_etc1_to_dxt1_selector_mappings[m][s]];
							total_err += err * err;
						}
						if (total_err < best_err)
						{
							LoHi *data = static_cast<LoHi *>(neighbors[i].data);
							best_lo = data->lo;
							best_hi = data->hi;
							best_err = total_err;
						}
					}

					assert(best_err != UINT32_MAX);

					result[n] = (etc1_to_dxt1_56_solution){(uint8_t)best_lo, (uint8_t)best_hi, (uint16_t)best_err};

					n++;

				} // m
			} // sr
		} // g
	} // inten
}

//******************************** Entry Point ********************************/

/**
 * Tests the generation and benchmarks it.
 */
int main(int /*argc*/, char * /*argv*/[])
{
	// Run this once and compare the result to the known table
	create_etc1_to_dxt1_6_conversion_table();
	if (!verifyTable(result, known))
	{
		printf("Generated results don't match known values\n");
	}

	// Perform multiple runs and take the best time
	unsigned best = UINT32_MAX;
	for (int n = 10; n > 0; n--)
	{
		unsigned time = millis();
		create_etc1_to_dxt1_6_conversion_table();
		time = millis() - time;
		if (time < best)
		{
			best = time;
		}
	}

	printf("Best run took %dms\n", best);
	return 0;
}
