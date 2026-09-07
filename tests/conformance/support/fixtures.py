"""仅用于 D0.06-c harness 自证的合法同步 mock 与故障后端。"""
class ContractError(RuntimeError):
    pass


class MockExecutor:
    def __init__(self):
        self.closed = False
        self.active = 0
        self.in_worker = False
        self.callback_errors = 0

    def notify(self, done, result):
        try:
            done(result)
        except Exception:
            self.callback_errors += 1

    def submit(self, work, done):
        if self.closed:
            return 'Rejected'
        self.active += 1
        previous_worker = self.in_worker
        self.in_worker = True
        try:
            try:
                value = work()
                result = {'status': 'Completed', 'value': value}
            except Exception as error:
                result = {'status': 'Failed', 'error': str(error)}
            self.notify(done, result)
        finally:
            self.in_worker = previous_worker
            self.active -= 1
        return 'Accepted'

    def drain(self):
        if self.in_worker:
            raise ContractError('worker cannot wait on its own executor')
        return self.active == 0

    def shutdown(self):
        if self.in_worker:
            raise ContractError('worker cannot destroy its own executor')
        self.closed = True
        return self.drain()


class DuplicateCallbackExecutor(MockExecutor):
    def notify(self, done, result):
        super().notify(done, result)
        super().notify(done, result)
