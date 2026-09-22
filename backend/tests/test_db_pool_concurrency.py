"""Concurrencia del PgPool: `with pool` solapados jamás deben fugar.

Contexto: revisión 00027-jb9 en prod murió con
`postgres pool exhausted (10 connections)` — toda la parte autenticada
caída mientras `/health` seguía verde. Hipótesis: `PgPool.__enter__`
guarda la conexión en `self._released` (¡del pool, compartido!) y dos
peticiones solapadas liberan dos veces la misma mientras la otra se
pierde para siempre.

Sin PG real: opener falso. El leak es lógica del pool, no del servidor.
"""
import threading
import unittest

from backend.db import PgPool


class FakeConn:
    def __init__(self):
        self.closed = False

    def execute(self, sql):
        pass

    def rollback(self):
        pass

    def close(self):
        self.closed = True


def make_pool(max_size=3):
    pool = PgPool("postgresql://u@127.0.0.1:1/db", max_size=max_size,
                  acquire_timeout_s=2, connect_timeout_s=1)
    pool._opener = FakeConn
    return pool


class PoolConcurrencyTest(unittest.TestCase):
    def test_sequential_use_is_stable(self):
        pool = make_pool()
        for _ in range(10):
            with pool as conn:
                conn.execute("SELECT 1")

    def test_overlapping_use_never_leaks(self):
        pool = make_pool()
        # Tantos hilos como conexiones: todos adquieren a la vez y se
        # solapan dentro del `with` (más hilos que conexiones sería
        # interbloqueo por diseño, no fuga: se quedarían esperando
        # hueco mientras los que lo tienen esperan la barrera).
        n_threads = 3
        rounds = 30
        barrier = threading.Barrier(n_threads)
        errors = []

        def worker():
            try:
                for _ in range(rounds):
                    barrier.wait(timeout=15)
                    with pool as conn:
                        conn.execute("SELECT 1")
                        barrier.wait(timeout=15)
            except Exception as exc:  # noqa: BLE001 - el test reporta
                errors.append(exc)

        threads = [threading.Thread(target=worker) for _ in range(n_threads)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=60)
        self.assertEqual(errors, [])
        # Las max_size conexiones deben ser recuperables y DISTINTAS:
        # con la fuga, alguna sigue fuera (timeout) o hay duplicadas
        # por doble-release de la misma.
        conns = [pool.acquire() for _ in range(3)]
        self.assertEqual(len({id(conn) for conn in conns}), 3)


if __name__ == "__main__":
    unittest.main()
