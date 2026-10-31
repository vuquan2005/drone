.DEFAULT_GOAL := help
PYTHON ?= python3
PRE_COMMIT ?= pre-commit

FORMAT_HOOKS := prettier black clang-format cmake-format format-latex
LINT_HOOKS := markdownlint-cli2 cmake-lint lint-latex

.PHONY: help format lint sync-papers sync-datasheets

help:
	@printf '%s\n' \
	  'make format           Format maintained source and document files' \
	  'make lint             Lint Markdown, CMake and LaTeX' \
	  'make sync-papers      Extract paper PDFs to Markdown and images' \
	  'make sync-datasheets  Extract datasheet PDFs to Markdown and images'

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
