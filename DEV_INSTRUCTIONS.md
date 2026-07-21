# CAC Platform 本地开发说明

当前项目包含两个本地服务：

- `cac-backend/`: Java Spring Boot backend
- `python-worker/`: minimal Python Kafka worker

本地开发辅助脚本是：

```bash
scripts/dev-reset.sh
```

这个脚本只用于 local development，不要用于 production。

## Reset Script 会做什么

运行：

```bash
scripts/dev-reset.sh --yes
```

脚本会执行这些操作：

1. 停止由该脚本启动过的 backend 和 worker 进程。
2. 停止占用 backend port `8080` 的进程。
3. 删除并重新创建 PostgreSQL database `cac_platform`。
4. 删除 Redis 中匹配 `job:*` 的 keys。
5. 删除并重新创建 Kafka topic `cac.analysis.requested`。
6. 尝试删除 Kafka consumer group `segment-cacs-worker-dev`。

database 被重新创建后，下次启动 Spring Boot backend 时，Flyway 会自动重新创建表，比如 `analysis_jobs` 和 `flyway_schema_history`。

## 常用命令

只 reset 本地状态：

```bash
cd /Users/nick/Desktop/java_backend
scripts/dev-reset.sh --yes
```

reset 本地状态，并自动启动 backend + worker：

```bash
cd /Users/nick/Desktop/java_backend
scripts/dev-reset.sh --yes --start
```

查看 backend log：

```bash
tail -f logs/backend.log
```

查看 worker log：

```bash
tail -f logs/worker.log
```

## Script Args

### `--yes`

必填。

这个参数表示你确认要执行 destructive local reset。

因为脚本会删除 PostgreSQL database、Redis keys、Kafka topic，所以没有 `--yes` 时脚本会拒绝运行。

用法：

```bash
scripts/dev-reset.sh --yes
```

### `--start`

可选。

加上这个参数后，脚本会在 reset 完成后自动启动：

- Spring Boot backend
- Python Kafka worker

用法：

```bash
scripts/dev-reset.sh --yes --start
```

## Environment Variables

通常不需要设置这些变量。默认值已经匹配当前本地开发配置。

只有当你的本机配置和默认值不同的时候，才需要设置它们。

### `DB_NAME`

PostgreSQL database name。

默认值：

```bash
cac_platform
```

示例：

```bash
DB_NAME=cac_platform_test scripts/dev-reset.sh --yes
```

### `DB_USER`

PostgreSQL username。

默认值：

```bash
nick
```

示例：

```bash
DB_USER=postgres scripts/dev-reset.sh --yes
```

### `DB_MAINTENANCE_NAME`

用于执行 `dropdb` / `createdb` 的 PostgreSQL maintenance database。

默认值：

```bash
postgres
```

通常不用改。

示例：

```bash
DB_MAINTENANCE_NAME=postgres scripts/dev-reset.sh --yes
```

### `REDIS_KEY_PATTERN`

要删除的 Redis key pattern。

默认值：

```bash
job:*
```

它会删除类似这些 keys：

```text
job:1:status
job:1:progress
```

示例：

```bash
REDIS_KEY_PATTERN='job:*' scripts/dev-reset.sh --yes
```

### `KAFKA_BOOTSTRAP_SERVERS`

Kafka bootstrap server。

默认值：

```bash
localhost:9092
```

示例：

```bash
KAFKA_BOOTSTRAP_SERVERS=localhost:9092 scripts/dev-reset.sh --yes
```

### `KAFKA_TOPIC`

用于 analysis request 的 Kafka topic。

默认值：

```bash
cac.analysis.requested
```

示例：

```bash
KAFKA_TOPIC=cac.analysis.requested scripts/dev-reset.sh --yes
```

### `KAFKA_GROUP_ID`

Python worker 使用的 Kafka consumer group。

默认值：

```bash
segment-cacs-worker-dev
```

示例：

```bash
KAFKA_GROUP_ID=segment-cacs-worker-dev scripts/dev-reset.sh --yes
```

### `BACKEND_PORT`

backend HTTP port。

默认值：

```bash
8080
```

如果想用 `8081`：

```bash
BACKEND_PORT=8081 scripts/dev-reset.sh --yes --start
```

worker 会调用：

```text
http://localhost:$BACKEND_PORT
```

## Kafka Command Path

脚本会从你的 `PATH` 中查找：

```bash
kafka-topics
kafka-consumer-groups
```

如果找不到这些命令，可以设置 `KAFKA_HOME`：

```bash
KAFKA_HOME=/path/to/kafka scripts/dev-reset.sh --yes
```

Homebrew 安装 Kafka 时，可能是：

```bash
KAFKA_HOME=/opt/homebrew/opt/kafka/libexec scripts/dev-reset.sh --yes
```

## 推荐本地开发流程

先确认 Kafka、PostgreSQL、Redis 都已经启动。

然后执行：

```bash
cd /Users/nick/Desktop/java_backend
scripts/dev-reset.sh --yes --start
```

查看 backend 是否启动成功：

```bash
tail -f logs/backend.log
```

看到 Spring Boot started 之后，创建一个 job：

```bash
curl -X POST http://localhost:8080/api/jobs \
  -H 'Content-Type: application/json' \
  -d '{
    "modelName": "SEGMENT-CACS",
    "inputPath": "/tmp/cac/case002/dicom",
    "outputPath": "/tmp/cac/job002"
  }'
```

worker 应该会 consume Kafka event，并更新 job：

```text
PENDING -> RUNNING 10 -> SUCCESS 100
```

查看 worker log：

```bash
tail -f logs/worker.log
```

## Troubleshooting

### PostgreSQL Reset 失败

检查 PostgreSQL 是否启动：

```bash
pg_isready
```

检查当前 user 是否能连接：

```bash
psql -U nick -d postgres
```

如果你的 PostgreSQL username 不是 `nick`：

```bash
DB_USER=your_username scripts/dev-reset.sh --yes
```

### Redis Reset 失败

检查 Redis：

```bash
redis-cli ping
```

预期输出：

```text
PONG
```

### Kafka Topic Reset 失败

检查 Kafka：

```bash
kafka-topics --bootstrap-server localhost:9092 --list
```

如果提示 `kafka-topics` not found，说明 Kafka `bin/` 不在 `PATH` 里。

可以设置 `KAFKA_HOME`：

```bash
KAFKA_HOME=/opt/homebrew/opt/kafka/libexec scripts/dev-reset.sh --yes
```

### Backend 启动失败

查看 backend log：

```bash
tail -n 200 logs/backend.log
```

常见原因：

- PostgreSQL 没启动。
- Redis 没启动。
- Kafka 没启动。
- port `8080` 被其他进程占用。

### Worker 没收到 Message

查看 worker log：

```bash
tail -n 200 logs/worker.log
```

检查 topic 是否存在：

```bash
kafka-topics --bootstrap-server localhost:9092 --describe --topic cac.analysis.requested
```

然后重新调用 `POST /api/jobs` 创建一个新 job。

worker 使用的是：

```text
auto.offset.reset=latest
```

所以它通常只会等待新的 Kafka messages。如果 consumer group 已经存在旧 offset，则会从该 group 的 committed offset 继续消费。

