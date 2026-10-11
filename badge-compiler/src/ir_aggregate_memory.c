#include "ir_pass.h"
#include <inttypes.h>
#include <string.h>

void ir_aggregate_memory(ir_context *c, ir_module *m) {
  for (unsigned l = 0; l < m->line_count; l++) {
    ir_line *line = &m->lines[l];
    c->line = l + 1;
    if (!line->instruction)
      continue;
    unsigned p = line->op, end = line->first + line->count;
    const char *op = ir_text(m, p++);
    int store = !strcmp(op, "store");
    if (!store && strcmp(op, "load"))
      continue;
    ir_note(c, "candidate", "memory_access", 1);
    int vol = !strcmp(ir_text(m, p), "volatile");
    if (vol)
      p++;
    if (strcmp(ir_text(m, p), "[")) {
      ir_note(c, "skipped", "not_array_carrier", 1);
      continue;
    }
    uint64_t count;
    unsigned bits = ir_bits(ir_text(m, p + 3));
    if (!ir_integer(ir_text(m, p + 1), &count) ||
        strcmp(ir_text(m, p + 2), "x") || !bits ||
        strcmp(ir_text(m, p + 4), "]"))
      ir_error(c, "unsupported aggregate memory type");
    if (bits < 32 || count > 256 || !count)
      ir_error(c, "aggregate memory requires 1..256 i32/i64 carriers");
    unsigned bytes = bits / 8, total = (unsigned)count * bytes;
    if (total <= 8) {
      ir_note(c, "skipped", "backend_supported_carrier", 1);
      continue;
    }
    const char *type = ir_format(c, "[%" PRIu64 " x i%u]", count, bits);
    p += 5;
    const char *value = store ? ir_text(m, p++) : NULL;
    if (strcmp(ir_text(m, p++), ",") || strcmp(ir_text(m, p++), "ptr"))
      ir_error(c, "invalid aggregate memory operands");
    const char *address = ir_text(m, p++);
    if (*address != '%' && *address != '@' && strcmp(address, "null"))
      ir_error(c, "aggregate memory requires a simple address");
    if (store && *value != '%' && strcmp(value, "zeroinitializer") &&
        strcmp(value, "undef") && strcmp(value, "poison"))
      ir_error(c, "aggregate store requires an SSA/zero value");
    unsigned align = bytes;
    if (p < end) {
      uint64_t n;
      if (end - p != 3 || strcmp(ir_text(m, p), ",") ||
          strcmp(ir_text(m, p + 1), "align") ||
          !ir_integer(ir_text(m, p + 2), &n) || !n || (n & (n - 1)) ||
          n > 65536)
        ir_error(c, "unsupported aggregate memory suffix/alignment");
      align = (unsigned)n;
    }
    if (vol)
      ir_error(c, "wide volatile aggregate SSA memory unsupported");
    ir_note(c, "transformed", "wide_carrier_scalarized", 1);
    ir_note(c, "generated", "scalar_memory_access", count);
    size_t output_start = c->size;
    const char *previous = "poison";
    for (unsigned k = 0; k < count; k++) {
      const char *ptr = address;
      if (k) {
        ptr = ir_fresh(c, m, '%');
        ir_emit(c, "  %s = getelementptr i8, ptr %s, i32 %u\n", ptr, address,
                k * bytes);
      }
      unsigned part_align = align < bytes ? align : bytes;
      const char *scalar = ir_fresh(c, m, '%');
      if (store) {
        ir_emit(c, "  %s = extractvalue %s %s, %u\n", scalar, type, value, k);
        ir_emit(c, "  store i%u %s, ptr %s, align %u\n", bits, scalar, ptr,
                part_align);
      } else {
        ir_emit(c, "  %s = load i%u, ptr %s, align %u\n", scalar, bits, ptr,
                part_align);
        const char *next =
            k + 1 == count ? ir_text(m, line->first) : ir_fresh(c, m, '%');
        ir_emit(c, "  %s = insertvalue %s %s, i%u %s, %u\n", next, type,
                previous, bits, scalar, k);
        previous = next;
      }
    }
    line->replacement =
        ir_copy(c, c->output + output_start, c->size - output_start);
    c->size = output_start;
  }
}
