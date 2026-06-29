import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Optional

import requests
from dotenv import load_dotenv

load_dotenv()

try:
    from confluent_kafka import Consumer as ConfluentConsumer
    from confluent_kafka import KafkaError, KafkaException
except ImportError:
    ConfluentConsumer = None
    KafkaError = None
    KafkaException = RuntimeError

try:
    from kafka import KafkaConsumer as PythonKafkaConsumer
except ImportError:
    PythonKafkaConsumer = None


KAFKA_BOOTSTRAP_SERVERS = os.getenv("KAFKA_BOOTSTRAP_SERVERS", "localhost:9092")
KAFKA_TOPIC = os.getenv("KAFKA_TOPIC", "cac.analysis.requested")
KAFKA_GROUP_ID = os.getenv("KAFKA_GROUP_ID", "segment-cacs-worker-dev")
BACKEND_BASE_URL = os.getenv("BACKEND_BASE_URL", "http://localhost:6006")

WORKER_MODE = os.getenv("WORKER_MODE", "fake").strip().lower()
SEGMENTCACS_WRAPPER = os.getenv(
    "SEGMENTCACS_WRAPPER",
    "/root/autodl-tmp/SEGMENT-CACS/model-service-python/run_segmentcacs.py",
)
SEGMENTCACS_SRC = os.getenv("SEGMENTCACS_SRC", "/root/autodl-tmp/SEGMENT-CACS/src")
MODEL_PATH = os.getenv(
    "MODEL_PATH",
    "/root/autodl-tmp/SEGMENT-CACS/data/model/SegmentCACS_0001619_unet.pt",
)
DEFAULT_DEVICE = os.getenv("DEVICE", "cuda")
USE_ZERO_MODULE = os.getenv("USE_ZERO_MODULE", "false").strip().lower() in {
    "1",
    "true",
    "yes",
    "on",
}

running = True


class KafkaMessageAdapter:
    def __init__(self, message: Any) -> None:
        self._message = message

    def error(self) -> None:
        return None

    def topic(self) -> str:
        return str(self._message.topic)

    def partition(self) -> int:
        return int(self._message.partition)

    def offset(self) -> int:
        return int(self._message.offset)

    def value(self) -> bytes:
        return bytes(self._message.value)


class KafkaPythonConsumerAdapter:
    def __init__(self, consumer: Any) -> None:
        self._consumer = consumer
        self._topics: list[str] = []

    def subscribe(self, topics: list[str]) -> None:
        self._topics = topics
        self._consumer.subscribe(topics)

    def poll(self, timeout_seconds: float) -> Optional[KafkaMessageAdapter]:
        timeout_ms = max(int(timeout_seconds * 1000), 0)
        records = self._consumer.poll(timeout_ms=timeout_ms, max_records=1)
        if not records:
            return None
        for batch in records.values():
            if batch:
                return KafkaMessageAdapter(batch[0])
        return None

    def commit(self, message: Any = None) -> None:
        self._consumer.commit()

    def close(self) -> None:
        self._consumer.close()


def handle_shutdown(signum: int, frame: Any) -> None:
    global running
    running = False
    print("\n[worker] shutdown requested, closing consumer after current poll...")


def create_consumer() -> Any:
    if ConfluentConsumer is not None:
        print("[worker] kafka client=confluent-kafka")
        return ConfluentConsumer(
            {
                "bootstrap.servers": KAFKA_BOOTSTRAP_SERVERS,
                "group.id": KAFKA_GROUP_ID,
                "auto.offset.reset": "earliest",
                "enable.auto.commit": False,
            }
        )

    if PythonKafkaConsumer is not None:
        print("[worker] kafka client=kafka-python")
        consumer = PythonKafkaConsumer(
            bootstrap_servers=KAFKA_BOOTSTRAP_SERVERS,
            group_id=KAFKA_GROUP_ID,
            auto_offset_reset="earliest",
            enable_auto_commit=False,
            consumer_timeout_ms=1000,
        )
        return KafkaPythonConsumerAdapter(consumer)

    raise RuntimeError(
        "No Kafka client available. Install confluent-kafka or kafka-python in the worker environment."
    )


def update_job_status(job_id: int, status: str, progress: int) -> None:
    url = f"{BACKEND_BASE_URL}/api/jobs/{job_id}/status"
    payload = {"status": status, "progress": progress}

    print(f"[worker] PATCH {url} -> {payload}")
    response = requests.patch(url, json=payload, timeout=10)

    if not response.ok:
        raise RuntimeError(
            f"PATCH failed: status_code={response.status_code}, body={response.text}"
        )

    print(f"[worker] status update ok: jobId={job_id}, response={response.text}")


def post_job_result(job_id: int, payload: dict[str, Any]) -> None:
    url = f"{BACKEND_BASE_URL}/api/jobs/{job_id}/result"

    print(f"[worker] POST {url} -> {payload}")
    response = requests.post(url, json=payload, timeout=10)

    if not response.ok:
        raise RuntimeError(
            f"POST result failed: status_code={response.status_code}, body={response.text}"
        )

    print(f"[worker] result save ok: jobId={job_id}, response={response.text}")


def save_fake_job_result(job_id: int, output_path: str) -> None:
    output_dir = output_path.rstrip("/")
    payload = {
        "agatstonScore": 123.4,
        "riskGrade": "MILD",
        "resultJsonPath": f"{output_dir}/result.json",
        "aiMaskPath": f"{output_dir}/ai_mask.nii.gz",
        "correctedMaskPath": None,
        "reportPath": None,
    }
    post_job_result(job_id, payload)


def parse_event(raw_value: bytes) -> dict[str, Any]:
    try:
        event = json.loads(raw_value.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON message: {raw_value!r}") from exc

    required_fields = ("jobId", "modelName", "inputPath", "outputPath")
    missing_fields = [field for field in required_fields if field not in event]

    if missing_fields:
        raise ValueError(f"Kafka message is missing fields: {missing_fields}")

    return event


def process_event(event: dict[str, Any]) -> None:
    print("[worker] received Kafka event")
    print(f"[worker] WORKER_MODE={WORKER_MODE}")

    if WORKER_MODE == "real":
        process_real_event(event)
    elif WORKER_MODE == "fake":
        process_fake_event(event)
    else:
        raise ValueError(f"Unsupported WORKER_MODE={WORKER_MODE!r}")


def process_fake_event(event: dict[str, Any]) -> None:
    job_id = int(event["jobId"])
    model_name = event["modelName"]
    input_path = event["inputPath"]
    output_path = event["outputPath"]

    print(f"[worker] jobId={job_id}")
    print(f"[worker] modelName={model_name}")
    print(f"[worker] inputPath={input_path}")
    print(f"[worker] outputPath={output_path}")

    update_job_status(job_id, "RUNNING", 10)

    print("[worker] fake mode: simulating SEGMENT-CACS inference with sleep(3)")
    time.sleep(3)

    save_fake_job_result(job_id, output_path)
    update_job_status(job_id, "SUCCESS", 100)
    print(f"[worker] completed fake jobId={job_id}")


def process_real_event(event: dict[str, Any]) -> None:
    job_id = int(event["jobId"])
    model_name = event["modelName"]
    input_path = str(event["inputPath"])
    output_path = str(event["outputPath"])
    file_type = str(event.get("fileType") or event.get("file_type") or "dcm")
    device = str(event.get("device") or DEFAULT_DEVICE)

    print(f"[worker] jobId={job_id}")
    print(f"[worker] modelName={model_name}")
    print(f"[worker] inputPath={input_path}")
    print(f"[worker] outputPath={output_path}")
    print(f"[worker] fileType={file_type}")
    print(f"[worker] device={device}")
    print(f"[worker] SEGMENTCACS_WRAPPER={SEGMENTCACS_WRAPPER}")
    print(f"[worker] SEGMENTCACS_SRC={SEGMENTCACS_SRC}")
    print(f"[worker] MODEL_PATH={MODEL_PATH}")

    try:
        validate_real_paths(input_path, output_path)
        update_job_status(job_id, "RUNNING", 10)

        command = build_segmentcacs_command(input_path, output_path, file_type, device)
        print(f"[worker] command executed: {command}")

        completed = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        print(f"[worker] subprocess return code={completed.returncode}")
        print_stream_summary("stdout", completed.stdout)
        print_stream_summary("stderr", completed.stderr)

        if completed.returncode != 0:
            raise RuntimeError(f"SEGMENT-CACS wrapper failed with code {completed.returncode}")

        update_job_status(job_id, "RUNNING", 80)

        result_path = Path(output_path) / "result.json"
        print(f"[worker] result.json path={result_path}")
        result_payload = map_result_payload(result_path, Path(output_path))

        post_job_result(job_id, result_payload)
        print(f"[worker] posted result payload={result_payload}")

        update_job_status(job_id, "SUCCESS", 100)
        print(f"[worker] completed real jobId={job_id}")
    except Exception as exc:
        print(f"[worker] real job failed: {exc}", file=sys.stderr)
        try:
            update_job_status(job_id, "FAILED", 0)
        except Exception as status_exc:
            print(f"[worker] failed to mark job FAILED: {status_exc}", file=sys.stderr)
        raise


def validate_real_paths(input_path: str, output_path: str) -> None:
    checks = {
        "SEGMENTCACS_WRAPPER": Path(SEGMENTCACS_WRAPPER),
        "SEGMENTCACS_SRC": Path(SEGMENTCACS_SRC),
        "MODEL_PATH": Path(MODEL_PATH),
        "inputPath": Path(input_path),
    }
    for name, path in checks.items():
        if name in {"SEGMENTCACS_WRAPPER", "MODEL_PATH"} and not path.is_file():
            raise FileNotFoundError(f"{name} does not exist or is not a file: {path}")
        if name in {"SEGMENTCACS_SRC", "inputPath"} and not path.exists():
            raise FileNotFoundError(f"{name} does not exist: {path}")

    Path(output_path).mkdir(parents=True, exist_ok=True)


def build_segmentcacs_command(
    input_path: str, output_path: str, file_type: str, device: str
) -> list[str]:
    command = [
        sys.executable,
        SEGMENTCACS_WRAPPER,
        "--segmentcacs-src",
        SEGMENTCACS_SRC,
        "--model",
        MODEL_PATH,
        "--input",
        input_path,
        "--output",
        output_path,
        "--file-type",
        file_type,
        "--device",
        device,
    ]
    if USE_ZERO_MODULE:
        command.append("--use-zero-module")
    return command


def map_result_payload(result_path: Path, output_dir: Path) -> dict[str, Any]:
    if not result_path.is_file():
        raise FileNotFoundError(f"Missing result.json: {result_path}")

    try:
        result = json.loads(result_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"Malformed result.json: {result_path}") from exc

    files = result.get("files")
    if not isinstance(files, dict):
        files = {}

    ai_mask_path = resolve_output_file(output_dir, files.get("originalMask"))
    corrected_mask_path = resolve_output_file(output_dir, files.get("correctedMask"))

    return {
        "agatstonScore": result.get("totalAgatstonScore"),
        "riskGrade": result.get("riskGrade"),
        "resultJsonPath": str(result_path),
        "aiMaskPath": ai_mask_path,
        "correctedMaskPath": corrected_mask_path,
        "reportPath": None,
    }


def resolve_output_file(output_dir: Path, value: Any) -> Optional[str]:
    if not value:
        return None
    path = Path(str(value))
    if not path.is_absolute():
        path = output_dir / path
    return str(path)


def print_stream_summary(name: str, value: str) -> None:
    if not value:
        print(f"[worker] {name}: <empty>")
        return
    trimmed = value.strip()
    if len(trimmed) > 4000:
        trimmed = trimmed[:4000] + "...<truncated>"
    print(f"[worker] {name}: {trimmed}")


def main() -> int:
    signal.signal(signal.SIGINT, handle_shutdown)
    signal.signal(signal.SIGTERM, handle_shutdown)

    consumer = create_consumer()
    consumer.subscribe([KAFKA_TOPIC])

    print("[worker] starting CAC analysis worker")
    print(f"[worker] kafka.bootstrap.servers={KAFKA_BOOTSTRAP_SERVERS}")
    print(f"[worker] kafka.topic={KAFKA_TOPIC}")
    print(f"[worker] kafka.group.id={KAFKA_GROUP_ID}")
    print(f"[worker] backend.base.url={BACKEND_BASE_URL}")
    print(f"[worker] WORKER_MODE={WORKER_MODE}")
    print(f"[worker] SEGMENTCACS_WRAPPER={SEGMENTCACS_WRAPPER}")
    print(f"[worker] SEGMENTCACS_SRC={SEGMENTCACS_SRC}")
    print(f"[worker] MODEL_PATH={MODEL_PATH}")

    try:
        while running:
            message = consumer.poll(1.0)

            if message is None:
                continue

            if message.error():
                if message.error().code() == KafkaError._PARTITION_EOF:
                    continue
                raise KafkaException(message.error())

            print(
                "[worker] consumed message "
                f"topic={message.topic()} partition={message.partition()} offset={message.offset()}"
            )

            try:
                event = parse_event(message.value())
                process_event(event)
            except Exception as exc:
                print(f"[worker] failed to process message: {exc}", file=sys.stderr)
                continue

            consumer.commit(message=message)
            print(f"[worker] committed Kafka offset={message.offset()}")

    finally:
        consumer.close()
        print("[worker] stopped")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
