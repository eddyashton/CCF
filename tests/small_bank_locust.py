from locust import HttpLocust, TaskSet, task, between
import random
import json

num_accounts = 10


class UserBehavior(TaskSet):
    def do_post(self, method, params={}):
        r = self.client.post(
            f"/{method}",
            json={"jsonrpc": "2.0", "id": 0, "method": method, "params": params},
            cert=("user1_cert.pem", "user1_privk.pem"),
            verify="networkcert.pem",
            catch_response=True,
        )
        with r as response:
            if response.ok:
                body = json.loads(response.text)
                err = body.get("error")
                if err:
                    response.failure(err.get("message"))
                else:
                    return json.loads(response.text)
            else:
                raise RuntimeError(f"{response.status_code}: {response.text}")

    def setup(self):
        s = self.do_post(
            "users/SmallBank_create_batch",
            {"from": 0, "to": num_accounts, "checking_amt": 1000, "savings_amt": 1000,},
        )

        print("Initial balances:")
        for i in range(num_accounts):
            balance = self.get_balance(i)
            print(f"  {i}: {balance}")

    def teardown(self):
        print("Final balances:")
        for i in range(num_accounts):
            balance = self.get_balance(i)
            print(f"  {i}: {balance}")

    @task(1)
    def get_balance(self, account=None):
        if account is None:
            account = random.randrange(0, num_accounts)
        response = self.do_post("users/SmallBank_balance", {"name": str(account)})
        if response is not None:
            return response["result"]

    @task(1)
    def deposit_checking(self, account=None, value=None):
        if account is None:
            account = random.randrange(0, num_accounts)
        if value is None:
            value = random.randint(1, 50)
        self.do_post(
            "users/SmallBank_deposit_checking", {"name": str(account), "value": value},
        )

    @task(1)
    def transact_savings(self, account=None, value=None):
        if account is None:
            account = random.randrange(0, num_accounts)
        if value is None:
            value = random.randint(-50, 50)
        self.do_post(
            "users/SmallBank_transact_savings", {"name": str(account), "value": value},
        )

    @task(1)
    def amalgamate(self):
        src_account = random.randrange(0, num_accounts)
        dst_account = random.randrange(0, num_accounts - 1)
        if dst_account >= src_account:
            dst_account += 1
        self.do_post(
            "users/SmallBank_amalgamate",
            {"name_src": str(src_account), "name_dest": str(dst_account)},
        )

    @task(1)
    def write_check(self, account=None, value=None):
        if account is None:
            account = random.randrange(0, num_accounts)
        if value is None:
            value = random.randint(0, 50)
        self.do_post(
            "users/SmallBank_write_check", {"name": str(account), "value": value},
        )


class WebsiteUser(HttpLocust):
    task_set = UserBehavior
    wait_time = between(0, 0.1)
