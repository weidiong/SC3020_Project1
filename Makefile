
# Compiler and flags
CC = gcc
CFLAGS = -O2 -std=c11 -Wall -Wextra
LDFLAGS = -lm

# Target executables
TARGETS = storage build_bpt deletion

# Source files
STORAGE_SRC = storage.c
BPTREE_SRC = B+tree.c
DELETION_SRC = deletion.c

# Data files
GAMES_FILE = games.txt
DATABASE_FILE = database.bin
INDEX_FILE = bpt.idx

# Default target
all: $(TARGETS)

# Task 1: Build storage system
storage: $(STORAGE_SRC)
	@echo "=== Building Storage System ==="
	$(CC) $(CFLAGS) $(STORAGE_SRC) -o storage
	@echo "Storage system built successfully!"

# Task 2: Build B+ tree index
build_bpt: $(BPTREE_SRC)
	@echo "=== Building B+ Tree Index ==="
	$(CC) $(CFLAGS) $(BPTREE_SRC) -o build_bpt
	@echo "B+ tree builder built successfully!"

# Task 3: Build deletion program
deletion: $(DELETION_SRC)
	@echo "=== Building Deletion Program ==="
	$(CC) $(CFLAGS) $(DELETION_SRC) -o deletion
	@echo "Deletion program built successfully!"

# Complete workflow: Run all tasks in sequence
run-all: storage build_bpt deletion
	@echo "=== Running Complete Database Task Workflow ==="
	@echo ""
	@echo "Step 1: Loading data into database..."
	./storage
	@echo ""
	@echo "Step 2: Building B+ tree index..."
	./build_bpt $(DATABASE_FILE) $(INDEX_FILE)
	@echo ""
	@echo "Step 3: Running deletion task..."
	./deletion $(DATABASE_FILE) $(INDEX_FILE)
	@echo ""
	@echo "=== All tasks completed successfully! ==="

# Run individual tasks
run-storage: storage
	@echo "=== Running Storage Task ==="
	./storage

run-bptree: build_bpt
	@echo "=== Running B+ Tree Building Task ==="
	./build_bpt $(DATABASE_FILE) $(INDEX_FILE)

run-deletion: deletion
	@echo "=== Running Deletion Task ==="
	./deletion $(DATABASE_FILE) $(INDEX_FILE)

# Clean up generated files
clean:
	@echo "Cleaning up generated files..."
	rm -f $(TARGETS) $(TARGETS).exe
	rm -f $(DATABASE_FILE) $(INDEX_FILE)
	@echo "Cleanup complete!"

# Clean only executables (keep data files)
clean-exec:
	@echo "Cleaning up executables..."
	rm -f $(TARGETS) $(TARGETS).exe
	@echo "Executables cleaned!"

# Show help
help:
	@echo "Database Course Makefile"
	@echo "========================"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build all executables"
	@echo "  storage      - Build storage system"
	@echo "  build_bpt    - Build B+ tree index builder"
	@echo "  deletion     - Build deletion program"
	@echo "  run-all      - Run complete workflow (all tasks)"
	@echo "  run-storage  - Run only storage task"
	@echo "  run-bptree   - Run only B+ tree building"
	@echo "  run-deletion - Run only deletion task"
	@echo "  clean        - Remove all generated files"
	@echo "  clean-exec   - Remove only executables"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make run-all        # Run everything"
	@echo "  make run-storage    # Just load data"
	@echo "  make run-bptree     # Just build index"
	@echo "  make run-deletion   # Just run deletion"

# Show current status
status:
	@echo "=== Current Status ==="
	@echo "Executables:"
	@for target in $(TARGETS); do \
		if [ -f $$target ]; then \
			echo "  ✓ $$target"; \
		else \
			echo "  ✗ $$target"; \
		fi; \
	done
	@echo "Data files:"
	@for file in $(GAMES_FILE) $(DATABASE_FILE) $(INDEX_FILE); do \
		if [ -f $$file ]; then \
			echo "  ✓ $$file"; \
		else \
			echo "  ✗ $$file"; \
		fi; \
	done

# Phony targets
.PHONY: all run-all run-storage run-bptree run-deletion clean clean-exec help status

