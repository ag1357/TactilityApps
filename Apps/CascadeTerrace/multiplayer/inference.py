"""Server-only broker boundary. Results are proposals, never canonical writes.
No client receives an inference endpoint. Unknown providers fail closed.
"""
from dataclasses import dataclass


@dataclass(frozen=True)
class InferenceRequest:
    player_id: bytes
    npc_id: bytes
    authorized_view: tuple
    utterance: str


class MockBroker:
    def __init__(self):
        self.demand = 0

    async def propose(self, request):
        if len(request.utterance.encode()) > 512:
            raise ValueError("utterance capacity")
        self.demand += 1
        return {"text": "No additional observation.", "actions": ()}


def broker(endpoint):
    if endpoint == "mock":
        return MockBroker()
    raise ValueError("provider not implemented; world authority never calls a model directly")
