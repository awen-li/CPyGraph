from .base import Base

class Child(Base):
    def work(self):
        return super().work()
