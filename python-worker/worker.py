import json
import os
import signal
import sys
import time
from typing import Any

import requests
from confluent_kafka import Consumer, KafkaError, KafkaException


KAFKA_BOOTSTRAP_SERVERS = os.getenv("KAFKA_BOOTSTRAP_SERVERS", "localhost:9092")
KAFKA_TOPIC = os.getenv("KAFKA_TOPIC", "cac.analysis.requested")
KAFKA_GROUP_ID = os.getenv("KAFKA_GROUP_ID", "segment-cacs-worker-dev")
BACKEND_BASE_URL = os.getenv("BACKEND_BASE_URL", "http://localhost:8080")

running = True


def handle_shutdown(signum: int, frame: Any) -> None:
    global running
    running = False
    print("\n[worker] shutdown requested, closing consumer after current poll...")


def create_consumer() -> Consumer:
    return Consumer(
        {
            "bootstrap.servers": KAFKA_BOOTSTRAP_SERVERS,
            "group.id": KAFKA_GROUP_ID,
            "auto.offset.reset": "latest",
            "enable.auto.commit": False,
        }
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


def save_job_result(job_id: int, output_path: str) -> None:
    url = f"{BACKEND_BASE_URL}/api/jobs/{job_id}/result"
    output_dir = output_path.rstrip("/")
    payload = {
        "agatstonScore": 123.4,
        "riskGrade": "MILD",
        "resultJsonPath": f"{output_dir}/result.json",
        "aiMaskPath": f"{output_dir}/ai_mask.nii.gz",
        "correctedMaskPath": None,
        "reportPath": None,
    }

    print(f"[worker] POST {url} -> {payload}")
    response = requests.post(url, json=payload, timeout=10)

    if not response.ok:
        raise RuntimeError(
            f"POST result failed: status_code={response.status_code}, body={response.text}"
        )

    print(f"[worker] result save ok: jobId={job_id}, response={response.text}")


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
    job_id = int(event["jobId"])
    model_name = event["modelName"]
    input_path = event["inputPath"]
    output_path = event["outputPath"]

    print("[worker] received Kafka event")
    print(f"[worker] jobId={job_id}")
    print(f"[worker] modelName={model_name}")
    print(f"[worker] inputPath={input_path}")
    print(f"[worker] outputPath={output_path}")

    update_job_status(job_id, "RUNNING", 10)

    print("[worker] simulating SEGMENT-CACS inference with sleep(3)")
    time.sleep(3)

    save_job_result(job_id, output_path)
    update_job_status(job_id, "SUCCESS", 100)
    print(f"[worker] completed jobId={job_id}")


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
