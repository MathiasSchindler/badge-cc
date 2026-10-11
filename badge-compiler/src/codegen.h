#ifndef BADGE_COMPILER_CODEGEN_H
#define BADGE_COMPILER_CODEGEN_H

#include <stddef.h>

typedef enum {
  BADGE_FRAME_MATERIALIZED,
  BADGE_FRAME_DIRECT
} badge_frame_addressing;
typedef enum {
  BADGE_RESIDENCY_STACK,
  BADGE_RESIDENCY_LOCAL,
  BADGE_RESIDENCY_GLOBAL,
  BADGE_RESIDENCY_GLOBAL_FLEX,
  BADGE_RESIDENCY_GLOBAL_COMPACT
} badge_register_residency;
typedef struct {
  badge_frame_addressing frame_addressing;
  badge_register_residency register_residency;
} badge_codegen_options;
typedef enum {
  BADGE_SELECTION_LEGACY,
  BADGE_SELECTION_THUMB,
  BADGE_SELECTION_THUMB_REGISTERS,
  BADGE_SELECTION_THUMB_LAYOUT,
  BADGE_SELECTION_THUMB_COMPACT,
  BADGE_SELECTION_THUMB_SCHEDULE,
  BADGE_SELECTION_THUMB_RELAX,
  BADGE_SELECTION_THUMB_BRANCH,
  BADGE_SELECTION_THUMB_HOMES,
  BADGE_SELECTION_THUMB_CALLS,
  BADGE_SELECTION_THUMB_EFFECTS,
  BADGE_SELECTION_THUMB_FRAMES,
  BADGE_SELECTION_THUMB_CONTROL,
  BADGE_SELECTION_THUMB_OPERANDS,
  BADGE_SELECTION_THUMB_WIDTHS,
  BADGE_SELECTION_THUMB_CONSTANTS
} badge_scalar_selection;
typedef enum { BADGE_MEMORY_BYTE, BADGE_MEMORY_WORD } badge_memory_selection;

#define BADGE_DEFAULT_FRAME_ADDRESSING BADGE_FRAME_DIRECT
#define BADGE_DEFAULT_REGISTER_RESIDENCY BADGE_RESIDENCY_GLOBAL_COMPACT
#define BADGE_DEFAULT_SCALAR_SELECTION BADGE_SELECTION_THUMB_WIDTHS
#define BADGE_DEFAULT_MEMORY_SELECTION BADGE_MEMORY_WORD

/* The source is borrowed for this call. On success, free(*assembly) after use.
   On failure, diagnostics go to stderr and the outputs are NULL/zero.
   Output pointers must be non-NULL and must not own an earlier result.
   Unspecified policies use the shared defaults above. The fully explicit
   selections entry point also supports the original conservative policies.
   THUMB_COMPACT is an explicit size-oriented candidate, not a speed default.
   THUMB_EFFECTS inherits THUMB_CALLS; faster busy waits require timing review.
   THUMB_WIDTHS inherits THUMB_OPERANDS, without scheduling or flag-setting
   compact arithmetic; final branch widths are owned-assembler decisions.
   THUMB_CONSTANTS is an explicit RS01-RS06 candidate; shared-base absolute
   stores require a proven scalar linear leaf with no flag consumers.
   Ordered scalar wrappers retain a preserved base and forward the incoming
   argument without changing the call or its pre/post-store effects.
   Available finalized internal-callee register summaries can retain a
   constant in untouched r2; all unknown or replaceable callees use AAPCS.
   Bounded scalar callback loops may use spill-free low-bank allocation,
   coalesced byte pointers, direct byte arguments and flag-safe zero branches;
   unsupported or higher-pressure functions retry conservative allocation.
   Pure scalar shift/extract arguments may feed marked direct callbacks;
   narrow shifts require flags to be killed before observation.
   Closed unsigned counted leaves with proven operand-free assembly retain
   every explicit NOP and use stack-free ABI-register countdown selection.
   Faster MMIO setup still requires physical timing qualification. */
int badge_codegen_compile(const char *source, size_t length,
                          const char *filename, char **assembly,
                          size_t *assembly_size);
int badge_codegen_compile_with_options(const char *source, size_t length,
                                       const char *filename,
                                       badge_codegen_options options,
                                       char **assembly, size_t *assembly_size);
int badge_codegen_compile_with_scalar_selection(
    const char *source, size_t length, const char *filename,
    badge_codegen_options options, badge_scalar_selection selection,
    char **assembly, size_t *assembly_size);
int badge_codegen_compile_with_selections(
    const char *source, size_t length, const char *filename,
    badge_codegen_options options, badge_scalar_selection scalar_selection,
    badge_memory_selection memory_selection, char **assembly,
    size_t *assembly_size);
int badge_codegen_compile_with_frame_addressing(
    const char *source, size_t length, const char *filename,
    badge_frame_addressing frame_addressing, char **assembly,
    size_t *assembly_size);

#endif
