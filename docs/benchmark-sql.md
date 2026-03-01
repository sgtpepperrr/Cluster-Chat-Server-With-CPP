# Benchmark SQL Scripts

本仓库提供两个脚本用于压测前的测试数据准备与清理：

- `scripts/generate_bench_sql.sh`：生成测试用户、测试群和群成员关系 SQL。
- `scripts/cleanup_bench_sql.sh`：生成清理这批测试数据的 SQL。
- `scripts/rebuild_chat_schema.sh`：按仓库约束重建聊天表结构（会清空表数据）。

## 0. 先统一表结构（推荐）

如果你当前库是手工建表、没有外键约束，建议先重建一次：

```bash
REALLY_RESET_CHAT_DB=YES ./scripts/rebuild_chat_schema.sh
```

说明：

- 使用 `docs/rebuild_schema.sql` 重建 `user/friend/allgroup/groupuser/offlinemessage`
- 包含外键约束
- `allgroup.groupname` 不加 `UNIQUE`
- 这是破坏性操作，会删除这些表中的现有数据

## 1. 自增主键与 `userid` 是否需要指定

不强制需要指定。

- 默认模式（推荐）：不显式写 `user.id`，让 MySQL `AUTO_INCREMENT` 自动分配。
- 固定 ID 模式：通过 `--start-user-id` 显式指定连续用户 ID（适合需要确定 ID 区间的压测脚本）。

## 2. 是否提交到 Git

建议提交以下内容到 Git：

- `scripts/generate_bench_sql.sh`
- `scripts/cleanup_bench_sql.sh`
- 本说明文档

不建议提交生成结果（例如 `/tmp/bench_seed.sql`），这类文件是环境相关产物，通常按需生成即可。

## 3. 生成脚本用法

### 默认参数（200 用户 + 21 人群）

```bash
./scripts/generate_bench_sql.sh > /tmp/bench_seed.sql
```

默认值：

- `user_count=200`
- `group_member_count=21`
- `group_name=benchgroup`
- `group_desc=benchmark`
- `name_prefix=bench_`
- `password=bench123`
- `state=offline`

### 固定起始用户 ID（显式写入 `user.id`）

```bash
./scripts/generate_bench_sql.sh --start-user-id 10001 > /tmp/bench_seed_fixed_id.sql
```

### 常用自定义参数

```bash
./scripts/generate_bench_sql.sh \
  --user-count 500 \
  --group-member-count 50 \
  --group-name benchgroup \
  --group-desc benchmark \
  --name-prefix bench_ \
  --password bench123 \
  --state offline > /tmp/bench_seed.sql
```

### 导入数据库

```bash
mysql -u <user> -p chat < /tmp/bench_seed.sql
```

## 4. 清理脚本用法

### 生成清理 SQL

```bash
./scripts/cleanup_bench_sql.sh > /tmp/bench_cleanup.sql
```

默认清理规则：

- 删除 `allgroup.groupname='benchgroup'` 的群
- 删除用户名前缀为 `bench_` 的用户（`LIKE 'bench\_%'`）
- 先显式删除 `groupuser`/`friend`/`offlinemessage` 关联数据，再删群和用户
- 不依赖外键级联；即使你当前库没有 FK 约束也可使用

### 导入清理 SQL

```bash
mysql -u <user> -p chat < /tmp/bench_cleanup.sql
```

### 可选参数

```bash
./scripts/cleanup_bench_sql.sh \
  --group-name benchgroup \
  --name-prefix bench_ > /tmp/bench_cleanup.sql
```
