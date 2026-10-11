CC ?= cc
CPPFLAGS += -Isrc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
TEST_FLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined
LLVM_OBJCOPY ?= llvm-objcopy
LLVM_CLANG ?= clang
LLVM_LLD ?= ld.lld
LLVM_NM ?= llvm-nm
LLVM_OBJDUMP ?= llvm-objdump
LLVM_OPT ?= opt
QEMU_ARM ?= qemu-system-arm
export BADGE_TEST_MODE ?= oracle

OPT_SOURCES := src/ir_core.c src/ir_analysis.c src/ir_passes.c src/ir_constant_fold.c src/ir_aggregate_memory.c src/ir_wide_division.c src/ir_dead_instructions.c src/ir_scalar_promotion.c src/ir_mem2reg.c src/ir_sccp.c src/ir_instruction_combine.c src/ir_local_cse.c src/ir_loop_invariant.c
OPT_HEADERS := src/ir_pass.h src/ir_analysis.h src/ir_promotion.h src/ir_integer.h src/target.h
CODEGEN_SOURCES := src/codegen.c src/codegen_cfg.c src/codegen_frame.c src/codegen_thumb.c src/codegen_register.c src/codegen_global.c src/codegen_select.c src/codegen_schedule.c src/codegen_assembly.c src/codegen_operands.c
CODEGEN_HEADERS := src/codegen.h src/codegen_internal.h src/target.h src/thumb_immediate.h
COST_SOURCES := src/target_cost.c
COST_HEADERS := src/target_cost.h src/thumb_immediate.h
OPT_SOURCES += src/ir_target_strength.c $(COST_SOURCES)
OPT_SOURCES += src/ir_local_memory.c
OPT_SOURCES += src/ir_cross_function.c
OPT_HEADERS += $(COST_HEADERS)
CODEGEN_SOURCES += $(COST_SOURCES)
CODEGEN_HEADERS += $(COST_HEADERS)
OPT_SOURCES += src/ir_loop_admission.c src/assembly_cost.c $(filter-out $(COST_SOURCES),$(CODEGEN_SOURCES))
OPT_HEADERS += src/ir_loop_admission.h src/assembly_cost.h src/badge_asm.c src/badge_thumb.h src/badge_object.c src/badge_object.h $(CODEGEN_HEADERS)
TEST_BINS := build/elf2bin_test build/badge_elf_info_test build/badge_cpp_test build/badge_parse_test build/badge_sema_test build/badge_ir_test build/badge_opt_test build/ir_pipeline_test build/ir_analysis_test build/ir_dead_instructions_test build/ir_scalar_promotion_test build/ir_mem2reg_test build/ir_sccp_test build/ir_instruction_combine_test build/ir_local_cse_test build/ir_local_cse_collision_test build/badge_codegen_test build/badge_object_test build/badge_asm_test build/badge_link_test

TEST_BINS += build/ir_loop_invariant_test

.PHONY: all test test-owned test-oracle test-metrics test-fast-io
.PHONY: test-tilt-maze
.PHONY: test-datasheet test-datasheet-facts test-datasheet-experiments
.PHONY: test-target-cost
.PHONY: test-compact
.PHONY: test-scheduling
.PHONY: test-final-layout
.PHONY: test-target-strength
.PHONY: test-datasheet-qualification
.PHONY: test-pipeline-stages
.PHONY: test-application
build/application_test: tests/application_test.c ../src/tools/compiler_test/application.c ../src/tools/compiler_test/application.h ../src/tools/compiler_test/suite.h ../src/tools/tilt_maze.c ../src/platform/badger/graphics.c ../src/tools/diagnostics_logic.c | build
	$(CC) $(TEST_FLAGS) -DCOMPILER_TEST_APPLICATION -DCOMPILER_TEST_TIMING -I../src/platform/badger -I../src/tools tests/application_test.c ../src/platform/badger/graphics.c ../src/tools/diagnostics_logic.c -o $@

test-application: all build/application_test
	./build/application_test
	node --test tests/application-protocol.test.mjs tests/compiler-compare.test.mjs
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" QEMU_ARM="$(QEMU_ARM)" bash tests/application-oracle.sh
build/compiler_collector_test: tests/compiler_collector_test.c tests/fixtures/compiler_usb_mock/libusb.h tests/fixtures/compiler_usb_mock/compiler_test_build.h ../src/host/tools/compiler_test_host.c ../src/host/tools/usb_live_transport.h ../src/tools/compiler_test/suite.h ../src/tools/compiler_test/kernels.h ../src/usb/live_usb.h ../src/shared/usb_report.c ../src/shared/usb_report.h ../src/tools/diagnostics_logic.c ../src/tools/diagnostics_logic.h | build
	$(CC) $(TEST_FLAGS) -DLIVE_USB_COMPILER -Itests/fixtures/compiler_usb_mock tests/compiler_collector_test.c ../src/shared/usb_report.c ../src/tools/diagnostics_logic.c -o $@

test-datasheet-qualification: build/compiler_collector_test
	@dir=$$(mktemp -d) || exit $$?; ./build/compiler_collector_test "$$dir"; status=$$?; rm -f "$$dir/request.bin" "$$dir/a.bin" "$$dir/b.bin" || exit $$?; rmdir "$$dir" || exit $$?; exit $$status
	node --test tests/datasheet_qualification_test.mjs tests/compiler-compare.test.mjs
all: build/elf2bin build/badge-elf-info build/badge-cpp build/badge-parse build/badge-sema build/badge-ir build/badge-opt build/badge-ir-metrics build/badge-codegen build/badge-object build/badge-asm build/badge-link build/badge-sha256

test-pipeline-stages:
	node --test tests/pipeline-stages.test.mjs

ATTRIBUTION_BINARY ?= build/badge-codegen-attribution
$(ATTRIBUTION_BINARY): src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCG_ATTRIBUTION_TRACE $< $(CODEGEN_SOURCES) -o $@

.PHONY: test-backend-attribution
test-backend-attribution: all build/badge-codegen-attribution
	node --test tests/backend-attribution.test.mjs

.PHONY: test-small-function-attribution
test-small-function-attribution: all
	node --test tests/small-function-attribution.test.mjs

build/badge-codegen-branch-budget: src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DCG_REGISTER_WORK_LIMIT=1024 $< $(CODEGEN_SOURCES) -o $@

.PHONY: test-compare-branch
test-compare-branch: all build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/compare-branch.test.mjs tests/compare-branch-evidence.test.mjs

.PHONY: test-scalar-homes
test-scalar-homes: all build/badge-codegen-attribution build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/scalar-homes.test.mjs tests/compare-branch.test.mjs

.PHONY: test-ssa-expressions
test-ssa-expressions: all
	node --test tests/ssa-expression.test.mjs

.PHONY: test-local-memory
build/badge-opt-memory-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_LOCAL_MEMORY_WORK_LIMIT=8 $< $(OPT_SOURCES) -o $@

test-local-memory: all build/badge-opt-sanitize build/badge-opt-memory-budget
	node --test tests/local-memory.test.mjs

.PHONY: test-call-transport
test-call-transport: all build/badge-codegen-attribution build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/call-transport.test.mjs

.PHONY: test-assembly-effects
test-assembly-effects: build/badge-codegen build/badge-asm build/badge-ir build/badge-opt build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/assembly-effects.test.mjs tests/assembly-effects-evidence.test.mjs

.PHONY: test-lightweight-frames
test-lightweight-frames: all build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/lightweight-frames.test.mjs

.PHONY: test-control-selection
test-control-selection: all build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/control-selection.test.mjs

.PHONY: test-operand-selection
test-operand-selection: all build/badge-codegen-sanitize build/badge-codegen-branch-budget
	node --test tests/operand-selection.test.mjs

.PHONY: test-loop-scalar
build/badge-opt-loop-admission-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_LOOP_ADMISSION_WORK_LIMIT=8 $< $(OPT_SOURCES) -o $@

test-loop-scalar: build/badge-cpp build/badge-ir build/badge-opt build/badge-opt-sanitize build/badge-ir-metrics build/badge-codegen-sanitize build/badge-asm build/badge-opt-loop-budget build/badge-opt-loop-admission-budget build/ir_loop_invariant_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_loop_invariant_test
	node --test tests/loop-scalar.test.mjs tests/loop-scalar-evidence.test.mjs

.PHONY: test-cross-function
build/badge-opt-cross-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_CROSS_FUNCTION_WORK_LIMIT=8 $< $(OPT_SOURCES) -o $@

test-cross-function: build/badge-cpp build/badge-ir build/badge-opt build/badge-opt-sanitize build/badge-ir-metrics build/badge-codegen-sanitize build/badge-asm build/badge-opt-cross-budget
	node --test tests/cross-function.test.mjs tests/cross-function-evidence.test.mjs

.PHONY: test-boundary-evidence
test-boundary-evidence: all build/badge_asm_test build/badge_link_test
	node --test tests/boundary-evidence.test.mjs

.PHONY: test-pipeline-promotion
test-pipeline-promotion:
	node --test tests/pipeline-promotion.test.mjs tests/datasheet_qualification_test.mjs tests/compiler-compare.test.mjs tests/application-protocol.test.mjs

.PHONY: test-current-promotion
test-current-promotion:
	node --test tests/current-promotion.test.mjs tests/compiler-image-cache.test.mjs tests/current-promotion-evidence.test.mjs

test: test-current-promotion

.PHONY: test-promoted-defaults
test-promoted-defaults: all build/badge_codegen_test
	./build/badge_codegen_test --api
	node --test tests/promoted-defaults.test.mjs tests/build_owned_test.mjs

build/ir_target_strength_test: tests/ir_target_strength_test.c $(OPT_SOURCES) $(OPT_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(OPT_SOURCES) -o $@

build/badge-opt-strength-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_TARGET_STRENGTH_WORK_LIMIT=8 src/badge_opt.c $(OPT_SOURCES) -o $@

test-target-strength: all build/ir_target_strength_test build/badge-opt-sanitize build/badge-opt-strength-budget
	./build/ir_target_strength_test
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_OPT="$(LLVM_OPT)" LLVM_LLD="$(LLVM_LLD)" QEMU_ARM="$(QEMU_ARM)" node --test tests/target_strength_test.mjs

build/compact_selection_test: tests/compact_selection_test.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(CODEGEN_SOURCES) -o $@

test-compact: all build/badge-codegen-sanitize build/compact_selection_test
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_OBJCOPY="$(LLVM_OBJCOPY)" LLVM_OBJDUMP="$(LLVM_OBJDUMP)" node --test tests/compact_selection_test.mjs

.PHONY: test-width-selection
build/constant_selection_test: tests/constant_selection_test.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(CODEGEN_SOURCES) -o $@

.PHONY: test-constant-selection
test-constant-selection: all build/badge-codegen-sanitize build/constant_selection_test
	node --test tests/constant-selection.test.mjs tests/constant-selection-evidence.test.mjs

test: test-constant-selection

.PHONY: test-byte-transport
test-byte-transport: all build/badge-codegen-sanitize build/constant_selection_test
	node --test tests/byte-transport.test.mjs tests/byte-transport-evidence.test.mjs tests/byte-transport-hardware.test.mjs tests/red-square.test.mjs

test: test-byte-transport

.PHONY: test-command-wrapper
test-command-wrapper: all build/badge-codegen-sanitize
	node --test tests/command-wrapper.test.mjs tests/call-preservation.test.mjs tests/command-wrapper-evidence.test.mjs

test: test-command-wrapper

.PHONY: test-table-interpreter
test-table-interpreter: all build/badge-codegen-sanitize
	node --test tests/table-interpreter.test.mjs tests/table-interpreter-evidence.test.mjs

test: test-table-interpreter

.PHONY: test-pixel-fill
test-pixel-fill: all build/badge-codegen-sanitize build/constant_selection_test
	node --test tests/pixel-fill.test.mjs tests/pixel-fill-evidence.test.mjs tests/constant-selection.test.mjs

test: test-pixel-fill

.PHONY: test-counted-delay
test-counted-delay: all build/badge-codegen-sanitize
	node --test tests/counted-delay.test.mjs tests/counted-delay-evidence.test.mjs

test: test-counted-delay

.PHONY: test-backend-refactor
test-backend-refactor: all build/profile-codegen build/badge-codegen-attribution
	node --test tests/backend-refactor.test.mjs

test: test-backend-refactor

build/width_selection_test: tests/width_selection_test.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(CODEGEN_SOURCES) -o $@

test-width-selection: all build/badge-codegen-sanitize build/width_selection_test
	node --test tests/width-selection.test.mjs tests/width-selection-evidence.test.mjs

test: build/width_selection_test

build/scheduling_test: tests/scheduling_test.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(CODEGEN_SOURCES) -o $@

build/badge-codegen-schedule-budget: src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DCG_SCHEDULE_WORK_LIMIT=8 $< $(CODEGEN_SOURCES) -o $@

test-scheduling: all build/scheduling_test build/badge-codegen-sanitize build/badge-codegen-schedule-budget
	./build/scheduling_test
	node --test tests/scheduling_test.mjs

build/badge-asm-layout-budget: src/badge_asm.c src/badge_thumb.h src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OBJECT_LIBRARY -DAS_MAX_LAYOUT_PASSES=2 src/badge_asm.c src/badge_object.c -o $@

test-final-layout: all build/badge-codegen-sanitize build/badge-asm-layout-budget
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" node --test tests/final_layout_test.mjs

build/pdfkit-datasheet: tools/pdfkit-datasheet.swift | build
	swiftc -O $< -o $@

test-datasheet: build/pdfkit-datasheet
	node --test tests/datasheet_test.mjs tests/datasheet_pdfkit_test.mjs

test-datasheet-facts:
	node --test tests/datasheet_facts_test.mjs
	node tools/check-datasheet-facts.mjs --files

test-datasheet-experiments:
	node --test tests/datasheet_experiments_test.mjs

build/badge-sha256: src/badge_sha256.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/badge-sha256-sanitize: src/badge_sha256.c | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

test-fast-io: all
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" LLVM_OBJCOPY="$(LLVM_OBJCOPY)" \
		QEMU_ARM="$(QEMU_ARM)" bash tests/fast-io-oracle.sh

test-tilt-maze: all
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" \
		QEMU_ARM="$(QEMU_ARM)" bash tests/tilt-maze-oracle.sh

build/badge-elf-info: src/badge_elf_info.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/badge_elf_info_test: tests/badge_elf_info_test.c src/badge_elf_info.c | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-elf-info-sanitize: src/badge_elf_info.c | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-opt: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge_opt_test: tests/badge_opt_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/badge_opt_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-opt-sanitize: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-ir-metrics: tools/badge_ir_metrics.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tools/badge_ir_metrics.c $(OPT_SOURCES) -o $@

build/badge-ir-metrics-sanitize: tools/badge_ir_metrics.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) tools/badge_ir_metrics.c $(OPT_SOURCES) -o $@

build/target_cost_test: tests/target_cost_test.c $(COST_SOURCES) $(COST_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< $(COST_SOURCES) -o $@

build/badge-ir-metrics-cost-budget: tools/badge_ir_metrics.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_TARGET_COST_WORK_LIMIT=8 tools/badge_ir_metrics.c $(OPT_SOURCES) -o $@

test-target-cost: build/target_cost_test build/badge-ir-metrics build/badge-ir-metrics-sanitize build/badge-ir-metrics-cost-budget
	./build/target_cost_test
	node --test tests/target_cost_test.mjs

build/ir_pipeline_test: tests/ir_pipeline_test.c src/ir_core.c src/ir_analysis.c $(OPT_HEADERS) | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) tests/ir_pipeline_test.c src/ir_core.c src/ir_analysis.c -o $@

build/ir_analysis_test: tests/ir_analysis_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_analysis_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_dead_instructions_test: tests/ir_dead_instructions_test.c $(OPT_SOURCES) $(OPT_HEADERS) tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) tests/ir_dead_instructions_test.c $(OPT_SOURCES) -o $@

build/ir_scalar_promotion_test: tests/ir_scalar_promotion_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_scalar_promotion_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_mem2reg_test: tests/ir_mem2reg_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_mem2reg_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-opt-sccp-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_SCCP_WORK_LIMIT=8 src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_sccp_test: tests/ir_sccp_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h build/badge-opt-sccp-budget | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_sccp_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-opt-combine-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_COMBINE_WORK_LIMIT=8 src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_instruction_combine_test: tests/ir_instruction_combine_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h build/badge-opt-combine-budget | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_instruction_combine_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-opt-cse-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_CSE_WORK_LIMIT=8 src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-opt-loop-budget: src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DIR_LOOP_WORK_LIMIT=8 src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_loop_invariant_test: tests/ir_loop_invariant_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h build/badge-opt-loop-budget | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_loop_invariant_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_local_cse_test: tests/ir_local_cse_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h build/badge-opt-cse-budget | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY tests/ir_local_cse_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/ir_local_cse_collision_test: tests/ir_local_cse_test.c src/badge_opt.c $(OPT_SOURCES) $(OPT_HEADERS) src/host_io.h tests/test_mode.h build/badge-opt-cse-budget | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OPT_LIBRARY -DCSE_COLLISION_TEST -DIR_CSE_HASH_BITS=0 tests/ir_local_cse_test.c src/badge_opt.c $(OPT_SOURCES) -o $@

build/badge-codegen: src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/badge_codegen.c $(CODEGEN_SOURCES) -o $@

build/badge_codegen_test: tests/badge_codegen_test.c src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) tests/badge_codegen_test.c $(CODEGEN_SOURCES) -o $@

build/badge-codegen-sanitize: src/badge_codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) src/badge_codegen.c $(CODEGEN_SOURCES) -o $@

build/profile-codegen: tools/profile-codegen.c $(CODEGEN_SOURCES) $(CODEGEN_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g tools/profile-codegen.c $(CODEGEN_SOURCES) -o $@

build/badge-ir: src/badge_ir.c src/badge_sema.c src/badge_parse.c src/c_semantic.h src/c_frontend.h src/host_io.h src/c_lexer.h src/target.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBADGE_SEMA_LIBRARY -DBADGE_PARSE_LIBRARY src/badge_ir.c src/badge_sema.c src/badge_parse.c -o $@

build/badge_ir_test: tests/badge_ir_test.c src/badge_ir.c src/badge_sema.c src/badge_parse.c src/c_semantic.h src/c_frontend.h src/host_io.h src/c_lexer.h src/target.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-link: src/badge_link.c src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBADGE_OBJECT_LIBRARY src/badge_link.c src/badge_object.c -o $@

build/badge_link_test: tests/badge_link_test.c src/badge_link.c src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OBJECT_LIBRARY tests/badge_link_test.c src/badge_object.c -o $@

build/link_oracle: tools/link_oracle.c src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBADGE_OBJECT_LIBRARY tools/link_oracle.c src/badge_object.c -o $@

build/badge-asm: src/badge_asm.c src/badge_thumb.h src/thumb_immediate.h src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBADGE_OBJECT_LIBRARY src/badge_asm.c src/badge_object.c -o $@

build/badge_asm_test: tests/badge_asm_test.c src/badge_asm.c src/badge_thumb.h src/thumb_immediate.h src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) -DBADGE_OBJECT_LIBRARY tests/badge_asm_test.c src/badge_object.c -o $@

build/badge-object: src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/badge_object.c -o $@

build/badge_object_test: tests/badge_object_test.c src/badge_object.c src/badge_object.h src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/elf2bin: src/elf2bin.c src/host_io.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/elf2bin_test: tests/elf2bin_test.c src/elf2bin.c src/host_io.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-cpp: src/badge_cpp.c src/host_io.h src/c_lexer.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/badge_cpp_test: tests/badge_cpp_test.c src/badge_cpp.c src/host_io.h src/c_lexer.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-parse: src/badge_parse.c src/c_frontend.h src/host_io.h src/c_lexer.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/badge_parse_test: tests/badge_parse_test.c src/badge_parse.c src/c_frontend.h src/host_io.h src/c_lexer.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

build/badge-sema: src/badge_sema.c src/badge_parse.c src/c_semantic.h src/c_frontend.h src/host_io.h src/c_lexer.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBADGE_PARSE_LIBRARY src/badge_sema.c src/badge_parse.c -o $@

build/badge_sema_test: tests/badge_sema_test.c src/badge_sema.c src/badge_parse.c src/c_semantic.h src/c_frontend.h src/host_io.h src/c_lexer.h tests/test_mode.h | build
	$(CC) $(CPPFLAGS) $(TEST_FLAGS) $< -o $@

test-owned:
	$(MAKE) BADGE_TEST_MODE=owned test

test-metrics: all build/badge-ir-metrics-sanitize build/badge-elf-info-sanitize build/badge_elf_info_test
	./build/badge_elf_info_test
	node --test tests/benchmark_c06_test.mjs

test: all $(TEST_BINS) build/badge-sha256-sanitize build/profile-codegen build/badge-opt-loop-admission-budget build/badge-opt-cross-budget test-target-cost test-compact test-scheduling test-final-layout test-target-strength test-datasheet-qualification test-pipeline-stages test-application test-backend-attribution test-compare-branch test-scalar-homes test-ssa-expressions test-local-memory test-call-transport test-boundary-evidence test-pipeline-promotion
	./build/elf2bin_test
	./build/badge_elf_info_test
	./build/badge_cpp_test
	PARSE_TEST_CLANG="$(LLVM_CLANG)" ./build/badge_parse_test
	SEMA_TEST_CLANG="$(LLVM_CLANG)" ./build/badge_sema_test
	IR_TEST_OPT="$(LLVM_OPT)" ./build/badge_ir_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/badge_opt_test
	./build/ir_pipeline_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_analysis_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_dead_instructions_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_scalar_promotion_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_mem2reg_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_sccp_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_instruction_combine_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_local_cse_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_local_cse_collision_test
	OPT_TEST_LLVM="$(LLVM_OPT)" ./build/ir_loop_invariant_test
	bash tests/owned_pipeline_test.sh
	node --test tests/badge_sha256_test.mjs tests/build_owned_test.mjs tests/promoted-defaults.test.mjs tests/small-function-attribution.test.mjs tests/assembly-effects.test.mjs tests/assembly-effects-evidence.test.mjs tests/lightweight-frames.test.mjs tests/control-selection.test.mjs tests/operand-selection.test.mjs tests/loop-scalar.test.mjs tests/loop-scalar-evidence.test.mjs tests/cross-function.test.mjs tests/cross-function-evidence.test.mjs tests/width-selection.test.mjs tests/width-selection-evidence.test.mjs tests/profile_host_test.mjs tests/datasheet_test.mjs tests/datasheet_facts_test.mjs tests/datasheet_experiments_test.mjs
	./build/badge_codegen_test
	./build/badge_object_test
	./build/badge_asm_test
	./build/badge_link_test

test-oracle: override BADGE_TEST_MODE=oracle
test-oracle: test
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_OPT="$(LLVM_OPT)" bash tests/ir_oracle.sh
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_OPT="$(LLVM_OPT)" LLVM_LLD="$(LLVM_LLD)" \
		QEMU_ARM="$(QEMU_ARM)" bash tests/codegen_oracle.sh
	IR_OPTIMIZER=owned LLVM_CLANG="$(LLVM_CLANG)" LLVM_OPT="$(LLVM_OPT)" LLVM_LLD="$(LLVM_LLD)" \
		QEMU_ARM="$(QEMU_ARM)" bash tests/codegen_oracle.sh
	$(LLVM_OBJCOPY) -O binary build/elf2bin-fixture.elf build/fixture-llvm.bin
	./build/elf2bin -O binary build/elf2bin-fixture.elf build/fixture-ours.bin
	cmp build/fixture-llvm.bin build/fixture-ours.bin
	./build/badge-cpp -DCLI=7 -DREMOVED -UREMOVED tests/fixtures/cpp_oracle_fixture.c -o build/cpp-fixture-ours.i
	$(LLVM_CLANG) --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -std=c11 -ffreestanding \
		-E -DCLI=7 -DREMOVED -UREMOVED tests/fixtures/cpp_oracle_fixture.c -o build/cpp-fixture-llvm.i
	./build/badge-cpp --tokens build/cpp-fixture-ours.i -o build/cpp-fixture-ours.tokens
	./build/badge-cpp --tokens build/cpp-fixture-llvm.i -o build/cpp-fixture-llvm.tokens
	cmp build/cpp-fixture-ours.tokens build/cpp-fixture-llvm.tokens
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" \
		LLVM_OBJCOPY="$(LLVM_OBJCOPY)" bash tests/object_oracle.sh
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" \
		LLVM_OBJCOPY="$(LLVM_OBJCOPY)" bash tests/asm_oracle.sh
	LLVM_CLANG="$(LLVM_CLANG)" LLVM_LLD="$(LLVM_LLD)" \
		LLVM_OBJCOPY="$(LLVM_OBJCOPY)" LLVM_NM="$(LLVM_NM)" bash tests/link_oracle.sh

build:
	mkdir -p $@
