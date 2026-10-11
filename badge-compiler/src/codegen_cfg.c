#include "codegen_internal.h"
#include <string.h>

int cg_target_has_phi(cg_context *c, const char *target) {
  const char *name = cg_name(c, target);
  for (cg_block *b = c->fn->blocks; b; b = b->next) {
    cg_register_work(c);
    if (strcmp(b->name, name))
      continue;
    for (cg_inst *i = b->first; i; i = i->next) {
      cg_register_work(c);
      if (i->result && !strcmp(c->tokens[i->begin + 2].text, "phi"))
        return 1;
    }
    return 0;
  }
  cg_error(c, "branch target does not exist");
}
static int cg_plain_jump(cg_context *c, cg_inst *i) {
  return i && !i->result && i->end - i->begin == 3 &&
         !strcmp(c->tokens[i->begin].text, "br") &&
         !strcmp(c->tokens[i->begin + 1].text, "label");
}
static void cg_forward_boolean_edges(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS) return;
  unsigned jumps = 0;
  for (cg_function *f = c->functions; f; f = f->next)
    for (cg_block *b = f->blocks; b; b = b->next)
      if (cg_plain_jump(c, b->last)) jumps++;
  if (jumps > (1000000u - c->count) / 9)
    cg_error(c, "boolean edge token budget exceeded");
  unsigned original_count = c->count;
  cg_token *tokens = cg_alloc(c, (c->count + jumps * 9) * sizeof(*tokens));
  memcpy(tokens, c->tokens, c->count * sizeof(*tokens));
  c->tokens = tokens;
  for (cg_function *f = c->functions; f; f = f->next) {
    c->fn = f;
    cg_block **link = &f->blocks;
    while (*link) {
      cg_register_work(c);
      cg_block *b = *link;
      cg_inst *phi = b->first, *branch = phi ? phi->next : NULL;
      if (b == f->blocks || !phi || !phi->result || phi->result->type->bits != 1 ||
          strcmp(c->tokens[phi->begin + 2].text, "phi") ||
          !branch || branch != b->last || branch->result ||
          branch->end - branch->begin != 9 ||
          strcmp(c->tokens[branch->begin].text, "br") ||
          strcmp(c->tokens[branch->begin + 1].text, "i1") ||
          strcmp(c->tokens[branch->begin + 2].text, phi->result->name) ||
          strcmp(c->tokens[branch->begin + 3].text, ",") ||
          strcmp(c->tokens[branch->begin + 4].text, "label") ||
          strcmp(c->tokens[branch->begin + 6].text, ",") ||
          strcmp(c->tokens[branch->begin + 7].text, "label")) {
        link = &b->next;
        continue;
      }
      char *yes = c->tokens[branch->begin + 5].text;
      char *no = c->tokens[branch->begin + 8].text;
      int eligible = strcmp(cg_name(c, yes), b->name) &&
                     strcmp(cg_name(c, no), b->name) &&
                     !cg_target_has_phi(c, yes) && !cg_target_has_phi(c, no);
      unsigned uses = 0, predecessors = 0, incoming = 0;
      for (cg_block *p = f->blocks; p; p = p->next)
        for (cg_inst *i = p->first; i; i = i->next)
          for (unsigned k = i->begin + (i->result ? 3 : 0); k < i->end; k++) {
            cg_register_work(c);
            const char *text = c->tokens[k].text;
            if (!strcmp(text, phi->result->name)) uses++;
            if (*text == '%' && !strcmp(cg_name(c, text), b->name)) {
              if (i != p->last || !cg_plain_jump(c, i) || k != i->begin + 2)
                eligible = 0;
              predecessors++;
            }
          }
      unsigned at = phi->begin + 4;
      while (eligible && at < phi->end) {
        cg_register_work(c);
        if (at + 4 >= phi->end || strcmp(c->tokens[at].text, "[") ||
            strcmp(c->tokens[at + 2].text, ",") ||
            strcmp(c->tokens[at + 4].text, "]")) { eligible = 0; break; }
        const char *value = c->tokens[at + 1].text;
        if (*value != '%' && strcmp(value, "true") && strcmp(value, "false") &&
            strcmp(value, "0") && strcmp(value, "1")) { eligible = 0; break; }
        const char *name = cg_name(c, c->tokens[at + 3].text);
        cg_block *p = f->blocks;
        while (p && strcmp(p->name, name)) { cg_register_work(c); p = p->next; }
        if (!p || !cg_plain_jump(c, p->last) || p->last->begin >= original_count ||
            strcmp(cg_name(c, c->tokens[p->last->begin + 2].text), b->name))
          eligible = 0;
        for (unsigned previous = phi->begin + 4; previous < at; previous += 6) {
          cg_register_work(c);
          if (!strcmp(cg_name(c, c->tokens[previous + 3].text), name)) eligible = 0;
        }
        incoming++;
        at += 5;
        if (at < phi->end && strcmp(c->tokens[at++].text, ",")) eligible = 0;
      }
      if (!eligible || uses != 1 || !incoming || incoming != predecessors) {
        link = &b->next;
        continue;
      }
      for (at = phi->begin + 4; at < phi->end; at += 6) {
        const char *name = cg_name(c, c->tokens[at + 3].text);
        cg_block *p = f->blocks;
        while (strcmp(p->name, name)) { cg_register_work(c); p = p->next; }
        cg_inst *jump = p->last;
        char *value = c->tokens[at + 1].text;
        unsigned begin = c->count;
        if (*value == '%') {
          memcpy(c->tokens + begin, c->tokens + branch->begin, 9 * sizeof(*tokens));
          c->tokens[begin + 2].text = value;
          c->count += 9;
        } else {
          memcpy(c->tokens + begin, c->tokens + jump->begin, 3 * sizeof(*tokens));
          c->tokens[begin + 2].text =
              !strcmp(value, "true") || !strcmp(value, "1") ? yes : no;
          c->count += 3;
        }
        jump->begin = begin;
        jump->end = c->count;
      }
      *link = b->next;
    }
  }
}
static void cg_sink_guarded_decrements(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS)
    return;
  for (cg_function *f = c->functions; f; f = f->next)
    for (cg_block *header = f->blocks; header; header = header->next) {
      cg_inst *previous = NULL, *decrement = header->first;
      while (decrement && decrement->result &&
             !strcmp(c->tokens[decrement->begin + 2].text, "phi")) {
        previous = decrement;
        decrement = decrement->next;
      }
      if (!previous || !decrement || !decrement->result ||
          decrement->end - decrement->begin != 7 ||
          strcmp(c->tokens[decrement->begin + 2].text, "sub") ||
          strcmp(c->tokens[decrement->begin + 3].text, "i32") ||
          strcmp(c->tokens[decrement->begin + 5].text, ",") ||
          strcmp(c->tokens[decrement->begin + 6].text, "1"))
        continue;
      cg_inst *compare = decrement->next, *branch = compare ? compare->next : NULL;
      if (!compare || !compare->result || compare->end - compare->begin != 8 ||
          strcmp(c->tokens[compare->begin + 2].text, "icmp") ||
          (strcmp(c->tokens[compare->begin + 3].text, "ne") &&
           strcmp(c->tokens[compare->begin + 3].text, "eq")) ||
          strcmp(c->tokens[compare->begin + 4].text, "i32") ||
          strcmp(c->tokens[compare->begin + 5].text, c->tokens[decrement->begin + 4].text) ||
          strcmp(c->tokens[compare->begin + 7].text, "0") ||
          !branch || branch != header->last || branch->end - branch->begin != 9 ||
          strcmp(c->tokens[branch->begin].text, "br") ||
          strcmp(c->tokens[branch->begin + 2].text, compare->result->name))
        continue;
      unsigned target = branch->begin +
          (!strcmp(c->tokens[compare->begin + 3].text, "ne") ? 5 : 8);
      const char *body_name = cg_name(c, c->tokens[target].text);
      cg_block *body = f->blocks;
      while (body && strcmp(body->name, body_name)) body = body->next;
      if (!body || body == header || !body->last || body->last == body->first ||
          body->last->end - body->last->begin != 3 ||
          strcmp(c->tokens[body->last->begin].text, "br") ||
          strcmp(cg_name(c, c->tokens[body->last->begin + 2].text), header->name))
        continue;
      unsigned predecessors = 0, references = 0;
      int safe = 1;
      for (cg_block *b = f->blocks; b; b = b->next)
        for (cg_inst *i = b->first; i; i = i->next) {
          cg_register_work(c);
          unsigned at = i->begin + (i->result ? 2 : 0);
          for (unsigned k = at + 1; k < i->end; k++) {
            if (k + 1 < i->end && !strcmp(c->tokens[k].text, "label") &&
                !strcmp(cg_name(c, c->tokens[k + 1].text), body_name))
              predecessors++;
            if (!strcmp(c->tokens[k].text, decrement->result->name)) {
              references++;
              if (b != header || strcmp(c->tokens[at].text, "phi") ||
                  !i->result || strcmp(i->result->name, c->tokens[decrement->begin + 4].text) ||
                  k + 2 >= i->end ||
                  strcmp(cg_name(c, c->tokens[k + 2].text), body_name))
                safe = 0;
            }
            if (b == body && !strcmp(c->tokens[k].text, "asm"))
              safe = 0;
          }
        }
      if (!safe || predecessors != 1 || references != 1)
        continue;
      cg_inst *last = body->first;
      while (last->next != body->last) {
        cg_register_work(c);
        last = last->next;
      }
      previous->next = compare;
      last->next = decrement;
      decrement->next = body->last;
    }
}
static void cg_delay_byte_advances(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS)
    return;
  for (cg_function *f = c->functions; f; f = f->next)
    for (cg_block *b = f->blocks; b; b = b->next) {
      cg_inst *previous = NULL;
      for (cg_inst *i = b->first; i && i->next;) {
        cg_inst *load = i->next;
        unsigned at = load->begin + (load->result ? 2 : 0);
        if (i->result && i->end - i->begin == 10 &&
            !strcmp(c->tokens[i->begin + 2].text, "getelementptr") &&
            !strcmp(c->tokens[i->begin + 3].text, "i8") &&
            !strcmp(c->tokens[i->begin + 8].text, "i32") &&
            !strcmp(c->tokens[i->begin + 9].text, "1") &&
            load->result && load->end - at == 8 &&
            !strcmp(c->tokens[at].text, "load") &&
            !strcmp(c->tokens[at + 1].text, "i8") &&
            !strcmp(c->tokens[at + 4].text, c->tokens[i->begin + 6].text)) {
          i->next = load->next;
          load->next = i;
          if (previous) previous->next = load;
          else b->first = load;
          if (b->last == load) b->last = i;
          previous = i;
          i = i->next;
        } else {
          previous = i;
          i = i->next;
        }
      }
    }
}
static void cg_remove_jump_blocks(cg_context *c) {
  if (c->scalar_selection != BADGE_SELECTION_THUMB_CONSTANTS) return;
  for (cg_function *f = c->functions; f; f = f->next) {
    c->fn = f;
    cg_block **link = &f->blocks;
    while (*link) {
      cg_register_work(c);
      cg_block *b = *link;
      if (b == f->blocks || b->first != b->last || !cg_plain_jump(c, b->last)) {
        link = &b->next;
        continue;
      }
      char *target = c->tokens[b->last->begin + 2].text;
      if (!strcmp(cg_name(c, target), b->name) || cg_target_has_phi(c, target)) {
        link = &b->next;
        continue;
      }
      int eligible = 1;
      for (cg_block *p = f->blocks; p; p = p->next)
        for (cg_inst *i = p->first; i; i = i->next)
          for (unsigned k = i->begin; k < i->end; k++) {
            cg_register_work(c);
            const char *text = c->tokens[k].text;
            if (*text == '%' && !strcmp(cg_name(c, text), b->name) &&
                (i->result || strcmp(c->tokens[i->begin].text, "br") ||
                 k == i->begin || strcmp(c->tokens[k - 1].text, "label")))
              eligible = 0;
          }
      if (!eligible) {
        link = &b->next;
        continue;
      }
      for (cg_block *p = f->blocks; p; p = p->next)
        for (cg_inst *i = p->first; i; i = i->next)
          for (unsigned k = i->begin; k < i->end; k++) {
            cg_register_work(c);
            const char *text = c->tokens[k].text;
            if (*text == '%' && !strcmp(cg_name(c, text), b->name))
              c->tokens[k].text = target;
          }
      *link = b->next;
    }
  }
}
void cg_prepare_cfg(cg_context *c) {
  cg_forward_boolean_edges(c);
  cg_remove_jump_blocks(c);
  cg_sink_guarded_decrements(c);
  cg_delay_byte_advances(c);
}
