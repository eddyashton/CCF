from locust import HttpLocust, TaskSet, task, between
import random
import json

n_accounts = 10


def do_post(client, method, params={}):
    return client.post(
        f"/{method}",
        json={"jsonrpc": "2.0", "id": 0, "method": method, "params": params},
        cert=("user1_cert.pem", "user1_privk.pem"),
        verify="networkcert.pem",
        catch_response=True,
    )


class UserBehavior(TaskSet):
    # TODO: Create accounts on startup
    @task(1)
    def get_balance(self):
        with do_post(
            self.client,
            "users/SmallBank_balance",
            {"name": str(random.randrange(0, n_accounts))},
        ) as response:
            if response.ok:
                body = json.loads(response.text)
                if body['error']:
                    response.failure(body['error']['message'])
            else:
                raise RuntimeError(f"{response.status_code}: {response.text}")


class WebsiteUser(HttpLocust):
    task_set = UserBehavior
    wait_time = between(0, 0.1)
