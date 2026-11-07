.DEFAULT_GOAL := help
PYTHON ?= python3
PRE_COMMIT ?= pre-commit
LATEXMK ?= latexmk
CUBEMX ?= $(if $(STM32CubeMX_PATH),$(STM32CubeMX_PATH),$(HOME)/STM32CubeMX)/STM32CubeMX
ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

FORMAT_HOOKS := prettier black clang-format cmake-format format-latex
LINT_HOOKS := markdownlint-cli2 cmake-lint lint-latex

.PHONY: help report cubemx clean format lint sync-papers sync-datasheets

help:
	@printf '%s\n' \
	  'make report                         Build report/build/main.pdf with latexmk' \
	  'make cubemx                         Regenerate firmware from drone.ioc (CLI)' \
	  'make clean                          Remove firmware and report build directories' \
	  'make format                         Format maintained source and document files' \
	  'make lint                           Lint Markdown, CMake and LaTeX' \
	  'make sync-papers                    Extract paper PDFs to Markdown and images' \
	  'make sync-datasheets                Extract datasheet PDFs to Markdown and images'

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
