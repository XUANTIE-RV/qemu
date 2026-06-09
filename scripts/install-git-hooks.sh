#!/bin/bash
#
# Script to install Git hooks for QEMU development
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOOKS_DIR="$REPO_ROOT/.git/hooks"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "Installing Git hooks for QEMU..."

# Create hooks directory if it doesn't exist
mkdir -p "$HOOKS_DIR"

# Install pre-commit hook
cat > "$HOOKS_DIR/pre-commit" << 'EOF'
#!/bin/bash
#
# Git pre-commit hook to run QEMU's checkpatch.pl on staged changes
#

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get the repository root
REPO_ROOT="$(git rev-parse --show-toplevel)"
CHECKPATCH="$REPO_ROOT/scripts/checkpatch.pl"

# Check if checkpatch.pl exists
if [ ! -f "$CHECKPATCH" ]; then
    echo -e "${RED}Error: checkpatch.pl not found at $CHECKPATCH${NC}"
    exit 1
fi

# Get the list of staged files
STAGED_FILES=$(git diff --cached --name-only --diff-filter=ACM)

if [ -z "$STAGED_FILES" ]; then
    echo -e "${YELLOW}No staged files to check${NC}"
    exit 0
fi

echo -e "${GREEN}Running checkpatch.pl on staged changes...${NC}"
echo ""

# Create a temporary file for the patch
TEMP_PATCH=$(mktemp)
git diff --cached > "$TEMP_PATCH"

# Run checkpatch.pl on the staged changes
if [ -s "$TEMP_PATCH" ]; then
    "$CHECKPATCH" --no-signoff "$TEMP_PATCH"
    CHECKPATCH_EXIT=$?

    # Clean up
    rm -f "$TEMP_PATCH"

    if [ $CHECKPATCH_EXIT -ne 0 ]; then
        echo ""
        echo -e "${RED}Checkpatch found issues with your changes.${NC}"
        echo -e "${YELLOW}Please fix the issues above or use 'git commit --no-verify' to bypass this check.${NC}"
        exit 1
    else
        echo ""
        echo -e "${GREEN}Checkpatch passed! Proceeding with commit.${NC}"
    fi
else
    echo -e "${YELLOW}No changes to check${NC}"
    rm -f "$TEMP_PATCH"
fi

exit 0
EOF

# Make the hook executable
chmod +x "$HOOKS_DIR/pre-commit"

# Install commit-msg hook
cat > "$HOOKS_DIR/commit-msg" << 'EOF'
#!/bin/bash
#
# Git commit-msg hook to check commit message format
#

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get the repository root
REPO_ROOT="$(git rev-parse --show-toplevel)"
CHECKPATCH="$REPO_ROOT/scripts/checkpatch.pl"

# Check if checkpatch.pl exists
if [ ! -f "$CHECKPATCH" ]; then
    echo -e "${RED}Error: checkpatch.pl not found at $CHECKPATCH${NC}"
    exit 1
fi

# The commit message file is passed as the first argument
COMMIT_MSG_FILE="$1"

echo -e "${GREEN}Checking commit message format...${NC}"

# Create a patch from the commit message for checkpatch.pl
# Use git format-patch style: From line + commit message + minimal diff
TEMP_PATCH=$(mktemp)
echo "From: $(git config user.name) <$(git config user.email)>" > "$TEMP_PATCH"
echo "Subject: $(head -1 "$COMMIT_MSG_FILE")" >> "$TEMP_PATCH"
echo "" >> "$TEMP_PATCH"
tail -n +2 "$COMMIT_MSG_FILE" >> "$TEMP_PATCH"
echo "" >> "$TEMP_PATCH"
echo "---" >> "$TEMP_PATCH"
echo "" >> "$TEMP_PATCH"
echo "diff --git a/commit-msg b/commit-msg" >> "$TEMP_PATCH"
echo "new file mode 100644" >> "$TEMP_PATCH"
echo "index 0000000..e69de29" >> "$TEMP_PATCH"
echo "--- /dev/null" >> "$TEMP_PATCH"
echo "+++ b/commit-msg" >> "$TEMP_PATCH"
echo "@@ -0,0 +1 @@" >> "$TEMP_PATCH"
echo "+commit message check" >> "$TEMP_PATCH"

# Run checkpatch.pl on the commit message
"$CHECKPATCH" "$TEMP_PATCH"
CHECKPATCH_EXIT=$?

# Clean up
rm -f "$TEMP_PATCH"

if [ $CHECKPATCH_EXIT -ne 0 ]; then
    echo ""
    echo -e "${RED}Commit message has style issues.${NC}"
    echo -e "${YELLOW}Please fix the issues above.${NC}"
    exit 1
else
    echo -e "${GREEN}Commit message check passed!${NC}"
fi

exit 0
EOF

# Make the hook executable
chmod +x "$HOOKS_DIR/commit-msg"

echo -e "${GREEN}✓ Pre-commit hook installed successfully${NC}"
echo -e "${GREEN}✓ Commit-msg hook installed successfully${NC}"
echo ""
echo "The hooks will automatically run checkpatch.pl:"
echo "  - pre-commit: checks code style of staged changes"
echo "  - commit-msg: checks commit message format (including Signed-off-by)"
echo ""
echo "To bypass the checks, use: git commit --no-verify"
echo ""
echo "For more information, see: docs/devel/pre-commit-hook.md"
