#!/usr/bin/env -S uv run --group tests --script

import json
import time
from websockets.sync.client import connect

import tinkerforge_util as tfutil
tfutil.create_parent_module(__file__, 'software')
from software.test_runner.test_context import run_testsuite, TestContext


def _connect_and_read_snapshot(host: str):
    client = connect(f'ws://{host}/ws', open_timeout=10, close_timeout=5)
    snapshot = ''

    try:
        while not snapshot.endswith('\n\n'):
            chunk = client.recv(timeout=10)
            if not isinstance(chunk, str):
                raise AssertionError('Expected a text WebSocket snapshot frame')

            snapshot += chunk
            if len(snapshot) > 2 * 1024 * 1024:
                raise AssertionError('WebSocket snapshot exceeded 2 MiB')

        messages = [json.loads(line) for line in snapshot.splitlines() if line]
        return client, messages
    except Exception:
        client.close()
        raise


def test_initial_snapshot_with_maximum_clients(tc: TestContext):
    clients = []

    try:
        for _ in range(5):
            client, messages = _connect_and_read_snapshot(tc._esp_host)
            clients.append(client)

            topics = {message['topic'] for message in messages}
            tc.assert_in('info/version', topics)
            tc.assert_gt(len(topics), 1)

        for client in clients:
            deadline = time.monotonic() + 5
            received_keep_alive = False

            while not received_keep_alive and time.monotonic() < deadline:
                try:
                    message = json.loads(client.recv(timeout=1))
                except TimeoutError:
                    continue
                received_keep_alive = message.get('topic') == 'info/keep_alive'

            tc.assert_(received_keep_alive)
    finally:
        for client in clients:
            client.close()


if __name__ == '__main__':
    run_testsuite(locals())
