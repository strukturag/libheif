/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "heif_colorconversion.h"
#include <typeinfo>

using namespace heif;


bool ColorState::operator==(const ColorState& b) const
{
  return (colorspace == b.colorspace &&
          chroma == b.chroma &&
          has_alpha == b.has_alpha &&
          bits_per_pixel == b.bits_per_pixel);
}


class Op_planar_to_RGB_8bit : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(ColorState input_state,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};



std::vector<ColorStateWithCost>
Op_planar_to_RGB_8bit::state_after_conversion(ColorState input_state,
                                              ColorState target_state,
                                              ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_RGB ||
      input_state.chroma != heif_chroma_444 ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGBA (with alpha)

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGBA;
  output_state.has_alpha = true;
  output_state.bits_per_pixel = 32;

  if (input_state.has_alpha == false &&
      target_state.has_alpha == false) {
    costs = { 0.1f, 0.0f, 0.25f };
  }
  else {
    costs = { 0.1f, 0.0f, 0.0f };
  }

  states.push_back({ output_state, costs });


  // --- convert to RGB (without alpha)

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_interleaved_RGB;
  output_state.has_alpha = false;
  output_state.bits_per_pixel = 24;

  if (input_state.has_alpha == true &&
      target_state.has_alpha == true) {
    // do not use this conversion because we would lose the alpha channel
  }
  else {
    costs = { 0.2f, 0.0f, 0.0f };
  }

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_planar_to_RGB_8bit::convert_colorspace(ColorState input_state,
                                          ColorState target_state,
                                          ColorConversionOptions options)
{
  return nullptr;
}





class Op_planar_YCbCr_to_RGB_8bit : public ColorConversionOperation
{
public:
  std::vector<ColorStateWithCost>
  state_after_conversion(ColorState input_state,
                         ColorState target_state,
                         ColorConversionOptions options) override;

  std::shared_ptr<HeifPixelImage>
  convert_colorspace(ColorState input_state,
                     ColorState target_state,
                     ColorConversionOptions options) override;
};



std::vector<ColorStateWithCost>
Op_planar_YCbCr_to_RGB_8bit::state_after_conversion(ColorState input_state,
                                                    ColorState target_state,
                                                    ColorConversionOptions options)
{
  if (input_state.colorspace != heif_colorspace_YCbCr ||
      input_state.chroma != heif_chroma_444 ||
      input_state.bits_per_pixel != 8) {
    return { };
  }

  std::vector<ColorStateWithCost> states;

  ColorState output_state;
  ColorConversionCosts costs;

  // --- convert to RGB

  output_state.colorspace = heif_colorspace_RGB;
  output_state.chroma = heif_chroma_444;
  output_state.has_alpha = input_state.has_alpha;  // we simply keep the old alpha plane
  output_state.bits_per_pixel = 8;

  costs = { 0.5f, 0.0f, 0.0f };

  states.push_back({ output_state, costs });

  return states;
}


std::shared_ptr<HeifPixelImage>
Op_planar_YCbCr_to_RGB_8bit::convert_colorspace(ColorState input_state,
                                                ColorState target_state,
                                                ColorConversionOptions options)
{
  return nullptr;
}





struct Node {
  int prev_processed_idx = -1;
  std::shared_ptr<ColorConversionOperation> op;
  ColorStateWithCost color_state;
};


bool ColorConversionPipeline::construct_pipeline(ColorState input_state,
                                                 ColorState target_state,
                                                 ColorConversionOptions options)
{
  m_operations.clear();

  if (input_state == target_state) {
    return true;
  }

  std::vector<std::shared_ptr<ColorConversionOperation>> ops;
  ops.push_back(std::make_shared<Op_planar_to_RGB_8bit>());
  ops.push_back(std::make_shared<Op_planar_YCbCr_to_RGB_8bit>());


  // --- Dijkstra search for the minimum-cost conversion pipeline

  std::vector<Node> processed_states;
  std::vector<Node> border_states;
  border_states.push_back({ -1, nullptr, { input_state, ColorConversionCosts() }});

  while (!border_states.empty()) {
    size_t minIdx;
    float minCost;
    for (size_t i=0;i<border_states.size();i++) {
      float cost = border_states[i].color_state.costs.total(options.criterion);
      if (i==0 || cost<minCost) {
        minIdx = i;
        minCost = cost;
      }
    }


    // move minimum-cost border_state into processed_states

    processed_states.push_back(border_states[minIdx]);

    border_states[minIdx] = border_states.back();
    border_states.pop_back();

    if (processed_states.back().color_state.color_state == target_state) {
      // end-state found, backtrack path to find conversion pipeline

      size_t idx = processed_states.size()-1;
      int len = 0;
      while (idx>0) {
        idx = processed_states[idx].prev_processed_idx;
        len++;
      }

      m_operations.resize(len);

      idx = processed_states.size()-1;
      int step = 0;
      while (idx>0) {
        m_operations[len-1-step] = processed_states[idx].op;
        idx = processed_states[idx].prev_processed_idx;
        step++;
      }

      for (const auto& op : m_operations) {
        printf("> %s\n",typeid(*op).name());
      }

      return true;
    }


    // expand the node with minimum cost

    for (size_t i=0;i<ops.size();i++) {
      auto out_states = ops[i]->state_after_conversion(processed_states.back().color_state.color_state,
                                                       target_state,
                                                       options);
      for (const auto& out_state : out_states) {
        bool state_exists = false;
        for (const auto& s : processed_states) {
          if (s.color_state.color_state==out_state.color_state) {
            state_exists = true;
            break;
          }
        }

        if (!state_exists) {
          for (auto& s : border_states) {
            if (s.color_state.color_state==out_state.color_state) {
              state_exists = true;

              // if we reached the same border node with a lower cost, replace the border node

              if (s.color_state.costs.total(options.criterion) > out_state.costs.total(options.criterion)) {
                s = { (int)(processed_states.size()-1),
                      ops[i],
                      out_state };

                s.color_state.costs = s.color_state.costs + processed_states.back().color_state.costs;
              }
              break;
            }
          }
        }


        // enter the new output state into the list of border states

        if (!state_exists) {
          ColorStateWithCost s = out_state;
          s.costs = s.costs + processed_states.back().color_state.costs;

          border_states.push_back({ (int)(processed_states.size()-1), ops[i], s });
        }
      }
    }
  }

  return false;
}
