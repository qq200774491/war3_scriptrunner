"""单宿主本地测试的全局 Hash、租约锁和广播；不连接平台全局数据库。"""
import json
from pathlib import Path
import secrets
import sqlite3
import time


def checked_text(value, limit):
    if not isinstance(value, str) or '\0' in value or len(value.encode('utf-8')) > limit:
        raise ValueError('invalid global archive text')
    return value


class GlobalArchive:
    def __init__(self, directory, clock=time.monotonic):
        if directory is None:
            raise ValueError('global archive requires explicit test archive directory')
        self.clock = clock
        self.locks = {}
        self.database = sqlite3.connect(Path(directory) / 'global.sqlite3')
        self.database.execute('PRAGMA synchronous=FULL')
        self.database.execute('CREATE TABLE IF NOT EXISTS fields '
                              '(key TEXT NOT NULL, field TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(key, field))')
        self.operations = {'MsGAHmSet': self.hmset, 'MsGAHmGet': self.hmget,
                           'MsGAHGetAll': self.hgetall, 'MsGAHDel': self.hdel,
                           'MsGALockAcquire': self.lock_acquire, 'MsGALockRelease': self.lock_release}

    def close(self):
        self.database.close()

    def request(self, transaction, operation, key, argument):
        try:
            checked_text(key, 256)
            if not key:
                raise ValueError('empty global archive key')
            if operation == 'MsBroadcast':
                checked_text(argument, 4096)
                return '_msbroadcast', {'key': key, 'value': argument}
            handler = self.operations.get(operation)
            if handler is None:
                raise ValueError('unsupported local global archive operation')
            with self.database:
                data = handler(key, argument)
                if len(json.dumps(data, ensure_ascii=False).encode('utf-8')) > 64 * 1024:
                    raise ValueError('global archive result exceeds 64 KiB')
            return '_msga_reply', dict(errnu=0, op=operation, trans_id=transaction, data=data)
        except ValueError as error:
            # 本地错误码明确表示失败，不冒充平台成功或平台错误码兼容。
            return '_msga_reply', dict(errnu=-1, op=operation, trans_id=transaction,
                                      data={}, local_error=str(error))

    def hash_available(self, key):
        if self.locks.get(key, 0) > self.clock():
            raise ValueError('key is a lock, not a hash')

    def fields(self, argument):
        fields = json.loads(argument)
        if not isinstance(fields, list) or not 1 <= len(fields) <= 100:
            raise ValueError('expected 1..100 field names')
        return [checked_text(field, 256) for field in fields]

    def hmset(self, key, argument):
        self.hash_available(key)
        values = json.loads(argument)
        if not isinstance(values, dict) or not values:
            raise ValueError('expected nonempty hash object')
        if len(argument.encode('utf-8')) > 40 * 1024:
            raise ValueError('hash batch exceeds 40 KiB')
        rows = [(key, checked_text(field, 256), checked_text(value, 4096))
                for field, value in values.items()]
        self.database.executemany('INSERT INTO fields VALUES (?, ?, ?) '
                                  'ON CONFLICT(key, field) DO UPDATE SET value=excluded.value', rows)
        return {'value': len(rows)}

    def hmget(self, key, argument):
        self.hash_available(key)
        values = {}
        for field in self.fields(argument):
            row = self.database.execute('SELECT value FROM fields WHERE key=? AND field=?', (key, field)).fetchone()
            if row:
                values[field] = row[0]
        return {'value': values}

    def hgetall(self, key, _):
        self.hash_available(key)
        return {'value': dict(self.database.execute('SELECT field, value FROM fields WHERE key=? ORDER BY field', (key,)))}

    def hdel(self, key, argument):
        self.hash_available(key)
        removed = 0
        for field in self.fields(argument):
            removed += self.database.execute('DELETE FROM fields WHERE key=? AND field=?', (key, field)).rowcount
        return {'value': removed}

    def lock_acquire(self, key, argument):
        if self.database.execute('SELECT 1 FROM fields WHERE key=? LIMIT 1', (key,)).fetchone():
            raise ValueError('key is a hash, not a lock')
        ttl = max(1000, min(300000, int(argument)))
        now = self.clock()
        if self.locks.get(key, 0) > now:
            return {'ok': False, 'token': '', 'ttl_ms': ttl}
        self.locks[key] = now + ttl / 1000
        return {'ok': True, 'token': secrets.token_hex(16), 'ttl_ms': ttl}

    def lock_release(self, key, _):
        if self.database.execute('SELECT 1 FROM fields WHERE key=? LIMIT 1', (key,)).fetchone():
            raise ValueError('key is a hash, not a lock')
        self.locks.pop(key, None)
        return {'ok': True}
