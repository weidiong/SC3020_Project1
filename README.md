# 🗄️ SC3020 Project 1 - Disk-Based B+ Tree Implementation

A complete implementation of a disk-based database system with B+ tree indexing, demonstrating storage, indexing, and deletion operations on NBA game statistics.

## 📁 Project Structure

```
├── storage.c          # Database storage system
├── B+tree.c          # B+ tree index builder
├── deletion.c        # Deletion with statistics
├── games.txt         # NBA game data (26,653 records)
├── Makefile          # Build automation (Unix/Linux)
├── README.md         # This file
└── COMPLETE_DELETION_SUMMARY.md  # Detailed implementation notes
```

## 📋 Project Overview

This project implements a three-stage database pipeline:

1. **Storage System** (`storage.c`) - Loads NBA game data into a disk-based database
2. **B+ Tree Index** (`B+tree.c`) - Builds an index on `FT_PCT_home` field
3. **Deletion System** (`deletion.c`) - Deletes records with `FT_PCT_home > 0.9` using the B+ tree

## 🚀 Quick Start

### Prerequisites

- **Visual Studio 2022** or **Visual Studio Build Tools 2022**
- **VS Code** or **Cursor IDE**
- **Windows 10/11** (or WSL for Linux development)

### 1. Open in VS Code

```bash
# Clone or download this project, then:
code .
# Or open the folder in VS Code/Cursor
```

### 2. Set Up Development Environment

#### Option A: Using Developer Command Prompt (Recommended)

1. Download MSVC Compiler at (https://visualstudio.microsoft.com/downloads/)
2. Open **Developer Command Prompt for VS 2022** from Start Menu
3. Navigate to project directory:

```cmd
cd "C:\path\to\your\project"
```

4. Build and Run All Programs

```cmd
.\build_and_run.bat
```

OR

4. Build Each Program Manually

```cmd
cl /O2 /std:c11 storage.c /Fe:storage.exe
cl /O2 /std:c11 B+tree.c /Fe:build_bpt.exe
cl /O2 /std:c11 deletion.c /Fe:deletion.exe
```

5. Run Complete Workflow

```cmd
# Step 1: Load data into database
.\storage.exe

# Step 2: Build B+ tree index
.\build_bpt.exe database.bin bpt.idx

# Step 3: Delete records and show statistics
.\deletion.exe database.bin bpt.idx
```

#### Option B: Using VS Code Terminal

1. Open VS Code terminal (` Ctrl + ``  `)
2. Initialize MSVC environment:

```cmd
cmd /k "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

### 3. Build All Programs

```cmd
cl /O2 storage.c /Fe:storage.exe
cl /O2 B+tree.c /Fe:build_bpt.exe
cl /O2 deletion.c /Fe:deletion.exe
```

### 4. Run Complete Workflow

```cmd
# Step 1: Load data into database
storage.exe

# Step 2: Build B+ tree index
build_bpt.exe database.bin bpt.idx

# Step 3: Delete records and show statistics
deletion.exe database.bin bpt.idx
```

## 🔧 Additional Build Commands

### Windows (MSVC)

# Clean build

del _.exe _.obj

```


## 🎯 Expected Output

### Stage 1: Storage System

```

Loaded 26652 records into the database.
Total blocks used: 314
Size of each record: 48 bytes
Sample of first block (up to 5 records):
Record 1: 22/12/2022, 1610612740, 126, 0.484
Record 2: 22/12/2022, 1610612762, 120, 0.488
Record 3: 21/12/2022, 1610612739, 114, 0.482
Record 4: 21/12/2022, 1610612755, 113, 0.441
Record 5: 21/12/2022, 1610612737, 108, 0.429

```

**Creates:** `database.bin` (~1.3MB)

### Stage 2: B+ Tree Index

```

B+ tree built.
Filtered out null keys: 0
Input records used: 26651

Actual B+ Tree stats:
Leaf nodes : 88
Internal nodes: 1
Tree height : 2 levels

Root is INTERNAL with 87 keys:
0.484 0.533 0.560 0.577 0.591 0.600 0.611 0.619 0.629 0.636 0.643 0.649 0.654 0.667 0.667 0.667 0.676 0.680 0.684 0.689 0.692 0.696 0.700 0.706 0.708 0.714 0.714 0.719 0.722 0.724 0.727 0.731 0.733 0.737 0.741 0.743 0.750 0.750 0.750 0.750 0.759 0.760 0.763 0.765 0.769 0.771 0.774 0.778 0.778 0.783 0.786 0.789 0.792 0.793 0.800 0.800 0.800 0.806 0.810 0.813 0.815 0.818 0.821 0.824 0.826 0.833 0.833 0.833 0.840 0.844 0.848 0.852 0.857 0.857 0.864 0.870 0.875 0.880 0.885 0.889 0.897 0.905 0.913 0.923 0.938 0.950 1.000
Leaf PID=0 has 304 keys
Leaf PID=1 has 304 keys
Leaf PID=2 has 304 keys
...

```

**Creates:** `bpt.idx` (~180KB)

### Stage 3: Deletion with Statistics

```

Step 3: Running deletion...
=== Task 3: Delete records with FT_PCT_home > 0.9 ===
Using COMPLETE B+ tree deletion:
ΓÇó Delete empty nodes (not just mark them)
ΓÇó Update parent keys when children removed
ΓÇó Maintain 90% fill factor (min: 304 entries/leaf)

=== Initial B+ Tree Statistics ===
Leaf nodes : 88
Internal nodes: 1
Tree height : 2 levels
Total entries : 26651

=== B+ Tree Deletion Statistics ===
Index nodes accessed : 88
Data blocks accessed : 281
Games deleted : 967
Entries removed from index : 967
Nodes merged : 3
Nodes actually DELETED : 3
Parent keys updated : 3
Average FT_PCT_home deleted : 0.933
Running time : 0.010000 seconds

=== Brute Force Comparison ===
Data blocks accessed (brute force): 314
Running time (brute force) : 0.001000 seconds
Speedup : 0.10x

=== Updated B+ Tree Statistics ===
Leaf nodes : 85 (was 88)
Internal nodes: 1
Total nodes : 86
Empty nodes : 0
Tree height : 2 levels
Total entries : 25684
Avg entries/leaf: 302.2

Root is INTERNAL with 84 keys:
Expected: 85 keys for 85 leaf nodes (N-1 rule)
0.484 0.533 0.560 0.577 0.591 0.600 0.611 0.619 0.629 0.636 0.643 0.649 0.654 0.667 0.667 0.667 0.676 0.680 0.684 0.689 ... (64 more)

=== Deletion complete! ===
3 nodes DELETED (zeroed out)
3 parent keys UPDATED
Tree structure maintained with proper N-1 key count

````

## 📊 Understanding the Results

| Metric                        | Description                          | Observed Value      |
| ----------------------------- | ------------------------------------ | ------------------- |
| Records loaded                | Total NBA game records processed     | 26,652              |
| Database size                 | Size of `database.bin`               | ~1.3MB (314 blocks) |
| Index size                    | Size of `bpt.idx`                    | ~180KB              |
| Leaf nodes                    | Number of leaf nodes in B+ tree      | 88                  |
| Internal nodes                | Number of internal nodes             | 1                   |
| Tree height                   | Number of levels in B+ tree          | 2 levels            |
| Games deleted                 | Records with `FT_PCT_home > 0.9`     | 967                 |
| Index nodes accessed          | Nodes accessed during deletion       | 88                  |
| Data blocks accessed          | Data blocks accessed during deletion | 281                 |
| Nodes deleted                 | Actual node removal                  | 3                   |
| Parent keys updated           | Internal keys updated                | 3                   |
| Average `FT_PCT_home` deleted | Mean value of deleted keys           | 0.933               |
| Speedup vs brute force        | Relative performance                 | 0.10x               |
| Total entries after deletion  | Remaining records in tree            | 25,684              |
| Average entries per leaf      | Entries per leaf post-deletion       | 302.2               |


⚡ Performance Summary
B+ Tree Efficiency: Slightly slower than brute force for this small deletion batch.
Index Access: Only 88 nodes accessed vs 314 data blocks.
Memory Usage: Efficient 4KB disk-based node storage.
Tree Maintenance: Proper node deletion and parent key updates maintain correct N-1 key structure.

## 🛠️ Troubleshooting

### Common Issues

#### "cl.exe not found"

```cmd
# Solution: Use Developer Command Prompt
# Or run this in regular cmd:
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
````

#### "games.txt not found"

```cmd
# Verify input file exists:
dir games.txt
# Should show: games.txt (26,653 bytes)
```

#### Empty output files

```cmd
# Check file sizes:
dir database.bin bpt.idx
# database.bin should be ~1.3MB, bpt.idx should be ~180KB
```

#### Compilation errors

```cmd
# Clean and rebuild:
del *.exe *.obj
cl /O2 /std:c11 storage.c /Fe:storage.exe
cl /O2 /std:c11 B+tree.c /Fe:build_bpt.exe
cl /O2 /std:c11 deletion.c /Fe:deletion.exe
```

## 📚 Technical Details

### Data Structures

- **Record Size:** 48 bytes (packed structure)
- **Block Size:** 4096 bytes (4KB)
- **Records per Block:** 85 records per block
- **Index Key:** `FT_PCT_home` (free throw percentage)

### Implementation Features

- **Buffer Pool:** LRU-based caching system
- **B+ Tree:** Balanced tree with 90% fill factor
- **Node Deletion:** Complete node removal (not just marking)
- **Parent Updates:** Automatic key updates in internal nodes
- **Statistics:** Comprehensive performance metrics

## 📄 Files Generated

After running the complete workflow:

- `storage.exe` - Database storage program
- `build_bpt.exe` - B+ tree index builder
- `deletion.exe` - Deletion with statistics
- `database.bin` - Binary database file (~1.3MB)
- `bpt.idx` - B+ tree index file (~180KB)

## 🔗 Additional Resources

- [COMPLETE_DELETION_SUMMARY.md](COMPLETE_DELETION_SUMMARY.md) - Detailed implementation notes
