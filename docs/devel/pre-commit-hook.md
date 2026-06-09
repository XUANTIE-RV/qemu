# QEMU Git Hooks 使用说明

## 功能说明

已为 QEMU 仓库配置了 Git hooks，在提交过程中自动运行 `scripts/checkpatch.pl` 检查代码规范：

- **pre-commit hook**: 检查暂存代码的风格
- **commit-msg hook**: 检查 commit message 格式（包括 Signed-off-by）

## Hook 位置

- `.git/hooks/pre-commit` - 代码风格检查
- `.git/hooks/commit-msg` - Commit message 格式检查

## 工作原理

### pre-commit hook
1. 当你执行 `git commit` 时，hook 会自动触发
2. Hook 会获取所有暂存的更改（staged changes）
3. 使用 `scripts/checkpatch.pl` 检查代码风格
4. 如果检查失败，提交被阻止

### commit-msg hook
1. 在 commit message 创建后触发（包括 `git commit -m` 或编辑器输入后）
2. 读取 `.git/COMMIT_EDITMSG` 中的 commit message
3. 使用 `scripts/checkpatch.pl` 检查 message 格式（包括 Signed-off-by）
4. 如果检查失败，提交被阻止

## 使用示例

### 正常提交（检查通过）
```bash
git add <files>
git commit -m "your commit message"
# Hook 会自动运行并显示：
# Running checkpatch.pl on staged changes...
# Checkpatch passed! Proceeding with commit.
```

### 检查失败的情况
```bash
git add <files>
git commit -m "your commit message"
# Hook 会显示错误信息，提交被阻止
# Checkpatch found issues with your changes.
# Please fix the issues above or use 'git commit --no-verify' to bypass this check.
```

## 绕过检查（不推荐）

如果你确实需要绕过检查（例如紧急修复），可以使用：
```bash
git commit --no-verify -m "your commit message"
```

## 手动运行检查

你也可以在提交前手动运行检查：
```bash
# 检查暂存的更改
git diff --cached | /workspaces/qemu/scripts/checkpatch.pl -

# 检查特定的 patch 文件
/workspaces/qemu/scripts/checkpatch.pl your-patch.patch

# 检查最近的提交
git format-patch -1 HEAD --stdout | /workspaces/qemu/scripts/checkpatch.pl -
```

## Hook 特性

- ✅ **pre-commit**: 自动检查所有暂存的代码变更
- ✅ **commit-msg**: 自动检查 commit message 格式（包括 Signed-off-by）
- ✅ 彩色输出（错误为红色，成功为绿色）
- ✅ 清晰的错误提示
- ✅ 支持 `--no-verify` 选项绕过所有检查
- ✅ 自动清理临时文件

## 注意事项

1. Hook 只检查暂存的更改（staged changes），未暂存的修改不会被检查
2. 建议在提交前先运行 `git add` 暂存你要提交的文件
3. 如果 checkpatch.pl 报告问题，请修复后再次提交
4. 遵循 QEMU 社区的代码规范可以提高补丁被接受的概率
