"""启动原版 DLL、两个 TCP 客户端并演示共享状态；结束后回收全部进程和连接。"""
import asyncio
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from shared_room.client import Client
from shared_room.server import RoomServer


async def main():
    server = RoomServer(ROOT / 'build/Release/script_host.exe', ROOT / 'scriptrunner.dll',
                        ROOT / 'tests/scripts/counter')
    clients = [Client(), Client()]
    transcript = []
    try:
        await server.start()
        for player, client in enumerate(clients):
            await client.connect(server.connection_info(player))
        for client in clients:
            await client.expect('ready')
        for player, request, event, payload in [(0, 'add-a', 'increment', '7'),
                                                 (1, 'add-b', 'increment', '5'),
                                                 (0, 'read-a', 'query', '')]:
            client = clients[player]
            await client.send(request, event, payload)
            result = await client.expect('event')
            ack = await client.expect('ack')
            entry = {'client': player, 'request': request, 'reply': result['payload'],
                     'sequence': ack['sequence']}
            transcript.append(entry)
            print(json.dumps(entry), flush=True)
        await clients[0].send('add-a', 'increment', '7')
        duplicate = await clients[0].expect('ack')
        assert duplicate['duplicate']
        transcript.append({'duplicate_retry': duplicate})
        print('Duplicate add-a reused sequence', duplicate['sequence'], flush=True)
        assert [item['reply'] for item in transcript[:3]] == ['7', '12', '12']
    finally:
        await server.close()
        for client in clients:
            await client.close()
    report = {'scope': 'Real vendor DLL + two TCP connections, no Warcraft clients',
              'host_exit_code': server.host.process.returncode, 'transcript': transcript,
              'logs': server.host.logs}
    (ROOT / 'analysis').mkdir(parents=True, exist_ok=True)
    (ROOT / 'analysis/demo-result.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print('PASS. Host exited:', server.host.process.returncode)


if __name__ == '__main__':
    asyncio.run(main())
