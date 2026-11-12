.DEFAULT_GOAL := help
PYTHON ?= python3
PRE_COMMIT ?= pre-commit
LATEXMK ?= latexmk
UV ?= uv
TELEMETRY_ARGS ?=
CUBE ?= cube
PRESET ?= Debug
SWD ?= port=SWD freq=1000 reset=SWrst
CUBEMX ?= $(if $(STM32CubeMX_PATH),$(STM32CubeMX_PATH),$(HOME)/STM32CubeMX)/STM32CubeMX
ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

FORMAT_HOOKS := prettier black clang-format cmake-format format-latex
LINT_HOOKS := markdownlint-cli2 cmake-lint lint-latex

.PHONY: telemetry help fw-build fw-flash fw-erase report cubemx clean format lint sync-papers sync-datasheets

help:
	@printf '%s\n' \
	  'make fw-build [PRESET=Release] Configure and build firmware (default: Debug)' \
	  'make fw-flash [PRESET=Release] Build, program, verify and reset via ST-LINK' \
	  'make fw-erase                 Erase ALL Flash, including stored settings' \
	  'make telemetry [TELEMETRY_ARGS=...] Open RTT plots, log and recording controls' \
	  'make report                         Build report/build/main.pdf with latexmk' \
	  'make cubemx                         Regenerate firmware from drone.ioc (CLI)' \
	  'make clean                          Remove firmware and report build directories' \
	  'make format                         Format maintained source and document files' \
	  'make lint                           Lint Markdown, CMake and LaTeX' \
	  'make sync-papers                    Extract paper PDFs to Markdown and images' \
	  'make sync-datasheets                Extract datasheet PDFs to Markdown and images'

fw-build:
	@case "$(PRESET)" in Debug|Release) ;; *) echo 'PRESET must be Debug or Release' >&2; exit 1;; esac
	@command -v "$(CUBE)" >/dev/null || { echo 'cube not found; add it to PATH or set CUBE=/path/to/cube' >&2; exit 1; }
	cd "$(ROOT_DIR)firmware/drone" && "$(CUBE)" cmake --preset "$(PRESET)"
	cd "$(ROOT_DIR)firmware/drone" && "$(CUBE)" cmake --build --preset "$(PRESET)"

fw-flash: fw-build
	"$(CUBE)" programmer -c $(SWD) -w "$(ROOT_DIR)firmware/drone/build/$(PRESET)/drone.elf" -v -rst

fw-erase:
	@command -v "$(CUBE)" >/dev/null || { echo 'cube not found; add it to PATH or set CUBE=/path/to/cube' >&2; exit 1; }
	"$(CUBE)" programmer -c $(SWD) -e all

report:
	cd "$(ROOT_DIR)report" && $(LATEXMK) main.tex

cubemx:
	@set -eu; script=$$(mktemp); trap 'rm -f "$$script"' EXIT HUP INT TERM; \
	printf '%s\n' \
	  'config load "$(ROOT_DIR)firmware/drone/drone.ioc"' \
	  'project path "$(ROOT_DIR)firmware/drone"' \
	  'project generate' \
	  'exit' > "$$script"; \
	"$(CUBEMX)" -q "$$script"

clean:
	$(RM) -r -- "$(ROOT_DIR)firmware/drone/build" "$(ROOT_DIR)report/build"

format: HOOKS = $(FORMAT_HOOKS)
lint: HOOKS = $(LINT_HOOKS)

format lint:
	@command -v $(PRE_COMMIT) >/dev/null || { echo 'Install first: uv tool install pre-commit'; exit 1; }
	@task_status=0; for hook in $(HOOKS); do \
	  $(PRE_COMMIT) run $$hook --hook-stage manual --all-files || task_status=1; \
	done; exit $$task_status

sync-papers:
	@$(PYTHON) tools/sync_reference_pdfs.py --collection papers

sync-datasheets:
	@$(PYTHON) tools/sync_reference_pdfs.py --collection datasheets

telemetry:
	cd "$(ROOT_DIR)" && "$(UV)" run --locked --group rtt tools/rtt/view.py $(TELEMETRY_ARGS)
