# CAC Platform AutoDL Server Instructions

这份文档用于 AutoDL / SeeTaCloud 服务器上的本地开发环境。当前代码路径：

```bash
cd ~/autodl-tmp/cac-platform
```

## 服务是否开机自启动？

目前不是可靠的开机自启动。

AutoDL/container 每天关机或重启后，以下进程通常都需要重新启动：

- `PostgreSQL`
- `Redis`
- `Kafka`
- Java Spring Boot backend
- Python Worker

其中 `PostgreSQL` 和 `Redis` 是系统包安装的，但在 container 环境里不要假设它们会自动起来。`Kafka`、backend、worker 都是手动进程，关机后一定会消失。

所以每天开机后建议直接运行一键脚本。

## 一键启动 server stack

启动 PostgreSQL、Redis、Kafka、backend：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/start-server-stack.sh
```

如果也想同时把 Python Worker 放到后台运行：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/start-server-stack.sh --with-worker
```

脚本是幂等的，重复运行不会重复启动已经存在的 Kafka/backend 进程。它会自动确保：

- PostgreSQL cluster 已启动
- PostgreSQL DB `cac_platform` 存在
- PostgreSQL user `nick` 存在
- Redis 已启动
- Kafka 已启动
- Kafka topic `cac.analysis.requested` 存在
- backend jar 不存在时自动执行 Maven build
- backend 启动在 `8080`

## 常用检查命令

检查服务器环境：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/server-check-and-setup.sh
```

检查进程：

```bash
ps aux | grep -E '[p]ostgres|[r]edis-server|[k]afka.Kafka|[c]ac-backend|[p]ython worker.py'
```

检查端口：

```bash
ss -ltnp | grep -E ':5432|:6379|:9092|:8080'
```

查看日志：

```bash
tail -f /tmp/kafka.log
tail -f /tmp/cac-backend.log
tail -f /tmp/cac-worker.log
```

## 单独启动命令

只启动 backend：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/run-backend.sh
```

只启动 Python Worker：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/run-worker.sh
```

开发时更推荐 worker 单独开一个 terminal 跑，这样能实时看它 consume Kafka event、PATCH status、POST result 的日志。

## 重要路径

项目路径：

```text
/root/autodl-tmp/cac-platform
```

SEGMENT-CACS 源码：

```text
/root/autodl-tmp/SEGMENT-CACS/src
```

当前找到的 model：

```text
/root/autodl-tmp/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt
```

本地开发数据目录：

```text
/data/cac/cases
/data/cac/jobs
/data/cac/masks
/data/cac/reports
/data/cac/results
/data/models
```

## 默认配置

backend 默认配置来自 `cac-backend/src/main/resources/application.yaml`，server 启动脚本会用 environment variables 覆盖必要项。

默认 server-side development values（一键启动脚本会使用本地 dev password；run-backend.sh 本身不写死 password）：

```text
SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/cac_platform
SPRING_DATASOURCE_USERNAME=nick
SPRING_DATASOURCE_PASSWORD=<由 scripts/start-server-stack.sh 设置，或你手动导出>
SPRING_KAFKA_BOOTSTRAP_SERVERS=localhost:9092
KAFKA_TOPIC=cac.analysis.requested
```

如需临时覆盖：

```bash
SPRING_DATASOURCE_PASSWORD=your_password bash scripts/start-server-stack.sh
```

## 从 Mac 访问 server backend

在 Mac 上开 SSH tunnel：

```bash
ssh -p 16733 -L 8080:localhost:8080 root@region-41.seetacloud.com
```

然后 Mac 浏览器或 Qt app 可以访问：

```text
http://localhost:8080
ws://localhost:8080/ws/jobs
```

## 快速链路测试

先启动服务：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/start-server-stack.sh
```

另一个 terminal 启动 worker：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/run-worker.sh
```

创建 job：

```bash
curl -X POST http://localhost:8080/api/jobs \
  -H 'Content-Type: application/json' \
  -d '{
    "modelName": "SEGMENT-CACS",
    "inputPath": "/data/cac/cases/case001/dicom",
    "outputPath": "/data/cac/jobs/job001"
  }'
```

假设返回 job id 是 `1`，查询 job：

```bash
curl http://localhost:8080/api/jobs/1
```

查询 result：

```bash
curl http://localhost:8080/api/jobs/1/result
```

如果 worker 正常消费，job 最后应该变成：

```json
{
  "status": "SUCCESS",
  "progress": 100
}
```

## Kafka 常用命令

查看 topic：

```bash
export PATH=/opt/kafka/bin:$PATH
kafka-topics.sh --bootstrap-server localhost:9092 --list
```

查看 topic offset：

```bash
export PATH=/opt/kafka/bin:$PATH
kafka-get-offsets.sh --bootstrap-server localhost:9092 --topic cac.analysis.requested
```

从头查看消息：

```bash
export PATH=/opt/kafka/bin:$PATH
kafka-console-consumer.sh \
  --bootstrap-server localhost:9092 \
  --topic cac.analysis.requested \
  --from-beginning \
  --max-messages 5
```

## 常见问题

### backend 启动失败，PostgreSQL password 相关

server 上 PostgreSQL TCP 连接要求 password。不要依赖空密码。用 `scripts/start-server-stack.sh` 初始化本地 dev DB，或手动导出 `SPRING_DATASOURCE_PASSWORD`。

如果你改过密码，同步这样启动：

```bash
SPRING_DATASOURCE_PASSWORD=your_password bash scripts/start-server-stack.sh
```

### worker 没收到 Kafka message

确认 worker 是在 `POST /api/jobs` 之前启动的。当前 worker 默认 `auto.offset.reset=latest`，更适合实时消费，不适合回放旧消息。

检查 topic 里是否有消息：

```bash
export PATH=/opt/kafka/bin:$PATH
kafka-console-consumer.sh \
  --bootstrap-server localhost:9092 \
  --topic cac.analysis.requested \
  --from-beginning \
  --max-messages 5
```

### confluent-kafka 安装失败

AutoDL 当前环境安装新版 `confluent-kafka` 可能会走源码编译并报 `rdkafka.h` 相关错误。服务器上当前 pin 到：

```text
confluent-kafka==2.4.0
```

重新安装：

```bash
cd ~/autodl-tmp/cac-platform/python-worker
source .venv/bin/activate
pip install -r requirements.txt
```

### 每天重启后的推荐顺序

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/start-server-stack.sh
bash scripts/run-worker.sh
```

如果只想一条命令后台启动全部：

```bash
cd ~/autodl-tmp/cac-platform
bash scripts/start-server-stack.sh --with-worker
```
