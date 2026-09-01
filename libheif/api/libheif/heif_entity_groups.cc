/*
 * HEIF codec.
 * Copyright (c) 2017-2025 Dirk Farin <dirk.farin@gmail.com>
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

#include "heif_entity_groups.h"
#include "box.h"
#include "api_structs.h"
#include "file.h"

#include <algorithm>
#include <memory>
#include <vector>


namespace {

enum class DuplicatePolicy {
  Reject,
  AllowLastEntityAsAlias,
};

heif_error add_entity_group(heif_context* ctx,
                            const std::shared_ptr<Box_EntityToGroup>& group,
                            const std::vector<heif_item_id>& item_ids,
                            bool require_image_items,
                            DuplicatePolicy duplicate_policy,
                            bool enforce_unique_alternative_membership,
                            heif_entity_group_id* out_group_id)
{
  if (out_group_id) {
    *out_group_id = 0;
  }

  const auto* limits = ctx->context->get_security_limits();
  if (limits->max_size_entity_group && item_ids.size() > limits->max_size_entity_group) {
    return {heif_error_Usage_error, heif_suberror_Security_limit_exceeded,
            "Entity group exceeds the configured security limit"};
  }

  auto file = ctx->context->get_heif_file();
  for (auto item = item_ids.begin(); item != item_ids.end(); ++item) {
    const heif_item_id id = *item;
    if (!file->get_infe_box(id)) {
      return {heif_error_Input_does_not_exist, heif_suberror_Nonexisting_item_referenced,
              "Entity group references a nonexisting item"};
    }

    if (std::find(item_ids.begin(), item, id) != item) {
      const bool is_allowed_alias =
          duplicate_policy == DuplicatePolicy::AllowLastEntityAsAlias &&
          item_ids.size() == 3 && item == item_ids.begin() + 2 &&
          (id == item_ids[0] || id == item_ids[1]);
      if (!is_allowed_alias) {
        return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value,
                "Entity group contains a duplicate item"};
      }
    }

    if (require_image_items && !ctx->context->is_image(id)) {
      return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value,
              "Stereo entity groups must contain image items"};
    }
  }

  if (enforce_unique_alternative_membership) {
    if (auto groups = file->get_grpl_box()) {
      for (const auto& box : groups->get_all_child_boxes()) {
        if (box->get_short_type() != fourcc("altr")) {
          continue;
        }

        auto alternative_group = std::dynamic_pointer_cast<Box_EntityToGroup>(box);
        if (!alternative_group) {
          continue;
        }

        const auto& existing_ids = alternative_group->get_item_ids();
        for (heif_item_id id : item_ids) {
          if (std::find(existing_ids.begin(), existing_ids.end(), id) != existing_ids.end()) {
            return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value,
                    "An entity may belong to only one alternative group"};
          }
        }
      }
    }
  }

  auto group_id = file->get_id_creator().get_new_id(IDCreator::Namespace::entity_group);
  if (!group_id) {
    return group_id.error_struct(ctx->context.get());
  }

  group->set_group_id(*group_id);
  group->set_item_ids(item_ids);
  file->add_entity_group_box(group);
  if (out_group_id) {
    *out_group_id = *group_id;
  }
  return heif_error_success;
}

}  // namespace


heif_entity_group* heif_context_get_entity_groups(const heif_context* ctx,
                                                  uint32_t type_filter,
                                                  heif_item_id item_filter,
                                                  int* out_num_groups)
{
  std::shared_ptr<Box_grpl> grplBox = ctx->context->get_heif_file()->get_grpl_box();
  if (!grplBox) {
    *out_num_groups = 0;
    return nullptr;
  }

  std::vector<std::shared_ptr<Box> > all_entity_group_boxes = grplBox->get_all_child_boxes();
  if (all_entity_group_boxes.empty()) {
    *out_num_groups = 0;
    return nullptr;
  }

  // --- filter groups

  std::vector<std::shared_ptr<Box_EntityToGroup> > entity_group_boxes;
  for (auto& group : all_entity_group_boxes) {
    if (type_filter != 0 && group->get_short_type() != type_filter) {
      continue;
    }

    auto groupBox = std::dynamic_pointer_cast<Box_EntityToGroup>(group);
    if (!groupBox) continue;
    const std::vector<heif_item_id>& items = groupBox->get_item_ids();

    if (item_filter != 0 && std::all_of(items.begin(), items.end(), [item_filter](heif_item_id item) {
      return item != item_filter;
    })) {
      continue;
    }

    entity_group_boxes.emplace_back(groupBox);
  }

  // --- convert to C structs

  auto* groups = new heif_entity_group[entity_group_boxes.size()];
  for (size_t i = 0; i < entity_group_boxes.size(); i++) {
    const auto& groupBox = entity_group_boxes[i];
    const std::vector<heif_item_id>& items = groupBox->get_item_ids();

    groups[i].entity_group_id = groupBox->get_group_id();
    groups[i].entity_group_type = groupBox->get_short_type();
    groups[i].entities = (items.empty() ? nullptr : new heif_item_id[items.size()]);
    groups[i].num_entities = static_cast<uint32_t>(items.size());

    if (groups[i].entities) {
      // avoid clang static analyzer false positive
      for (size_t k = 0; k < items.size(); k++) {
        groups[i].entities[k] = items[k];
      }
    }
  }

  *out_num_groups = static_cast<int>(entity_group_boxes.size());
  return groups;
}


void heif_entity_groups_release(heif_entity_group* grp, int num_groups)
{
  for (int i = 0; i < num_groups; i++) {
    delete[] grp[i].entities;
  }

  delete[] grp;
}


heif_error heif_context_add_alternative_entity_group(heif_context* ctx,
                                                     const heif_item_id* item_ids,
                                                     uint32_t num_items,
                                                     heif_entity_group_id* out_group_id)
{
  if (out_group_id) {
    *out_group_id = 0;
  }
  if (!ctx || !item_ids) {
    return heif_error_null_pointer_argument;
  }
  if (num_items == 0) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value,
            "Alternative entity group must not be empty"};
  }
  const auto* limits = ctx->context->get_security_limits();
  if (limits->max_size_entity_group && num_items > limits->max_size_entity_group) {
    return {heif_error_Usage_error, heif_suberror_Security_limit_exceeded,
            "Entity group exceeds the configured security limit"};
  }

  auto group = std::make_shared<Box_EntityToGroup>();
  group->set_short_type(fourcc("altr"));
  return add_entity_group(ctx, group, std::vector<heif_item_id>(item_ids, item_ids + num_items),
                          false, DuplicatePolicy::Reject, true,
                          out_group_id);
}


heif_error heif_context_add_stereo_pair_entity_group(heif_context* ctx,
                                                     heif_item_id left_image_id,
                                                     heif_item_id right_image_id,
                                                     heif_entity_group_id* out_group_id)
{
  if (out_group_id) {
    *out_group_id = 0;
  }
  if (!ctx) {
    return heif_error_null_pointer_argument;
  }

  auto group = std::make_shared<Box_ster>();
  return add_entity_group(ctx, group, {left_image_id, right_image_id}, true,
                          DuplicatePolicy::Reject, false,
                          out_group_id);
}


heif_error heif_context_add_stereo_pair_with_monoscopic_fallback_entity_group(
    heif_context* ctx,
    heif_item_id left_image_id,
    heif_item_id right_image_id,
    heif_item_id monoscopic_image_id,
    heif_entity_group_id* out_group_id)
{
  if (out_group_id) {
    *out_group_id = 0;
  }
  if (!ctx) {
    return heif_error_null_pointer_argument;
  }
  if (left_image_id == right_image_id) {
    return {heif_error_Usage_error, heif_suberror_Invalid_parameter_value,
            "Stereo pair left and right images must differ"};
  }

  auto group = std::make_shared<Box_stem>();
  return add_entity_group(ctx, group,
                          {left_image_id, right_image_id, monoscopic_image_id}, true,
                          DuplicatePolicy::AllowLastEntityAsAlias, false,
                          out_group_id);
}
