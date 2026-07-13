# 🤝 Contributing to DrutaDB

Thanks for your interest in contributing to DrutaDB! This document covers everything you need to get started.

## 🚀 Getting Started

1. **Fork** the repository
2. **Clone** your fork:
```bash
   git clone https://github.com/<your-username>/DrutaDB.git
   cd DrutaDB
```
3. Create a branch for your change:
```bash
   git checkout -b your-feature-name
```

## 📋 Requirements

- CMake 3.13+
- A C++23 compatible compiler (e.g., GCC 13+)
- Pthreads

## 🏗️ Building

```bash
mkdir build
cd build
cmake ..
make
./drutadb
```

## 🧪 Testing

Before opening a PR, run the test suite locally:

```bash
chmod +x ./test/benchmark_pro.sh
./test/benchmark_pro.sh
```

Make sure everything passes. Our CI will also run these checks automatically on your PR, along with build verification and sanitizer runs (ASan/UBSan) — but running locally first saves you a slower feedback loop.

## 🛠️ Making Changes

- Keep PRs focused — one logical change per PR is easier to review than a bundle of unrelated fixes.
- Follow the existing code style and structure in the file/module you're editing.
- If you're changing behavior (not just refactoring), add or update tests where relevant.
- Update the README if your change affects supported commands, build steps, or usage.

## ✍️ Commit Sign-off (DCO)

All commits must be signed off to certify you wrote or have the right to submit the code (Developer Certificate of Origin). Use the `-s` flag:

```bash
git commit -s -m "your commit message"
```

This appends a `Signed-off-by` line to your commit. PRs without signed-off commits will fail the DCO check and can't be merged. If you forgot to sign off, you can fix it with:

```bash
git commit --amend -s
git push --force-with-lease
```

## 📬 Submitting a Pull Request

1. Push your branch to your fork:
```bash
   git push origin your-feature-name
```
2. Open a PR against `main` in this repository.
3. Fill in a clear description of what the change does and why.
4. Link the relevant issue if one exists (e.g., "Closes #1").
5. Wait for CI to run — a maintainer will need to approve the workflow run if this is your first contribution here.
6. Address any review feedback. Once CI is green and the review is approved, it'll be merged.

## 🌱 Good First Issues

Look for issues labeled [`good first issue`](https://github.com/VasuBhakt/DrutaDB/issues?q=state%3Aopen%20label%3A%22good%20first%20issue%22) if you're new to the project. Comment on the issue to get it assigned to you before starting work.

## ❓ Questions

Open an issue if anything here is unclear or you run into trouble getting set up.