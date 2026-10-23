import json

import pytest
from testsuite.databases.pgsql import discover

pytest_plugins = [
    'pytest_userver.plugins.core',
    'pytest_userver.plugins.postgresql',
    'pytest_userver.plugins.redis',
]


@pytest.fixture(scope='session')
def pgsql_local(service_source_dir, pgsql_local_create):
    databases = discover.find_schemas(
        'courier_shifts', [service_source_dir / 'postgresql/schemas'],
    )
    return pgsql_local_create(list(databases.values()))


@pytest.fixture(scope='session')
def service_env(redis_standalone_node):
    secdist = {
        'redis_settings': {
            'courier-cache': {
                'password': '',
                'sentinels': [redis_standalone_node],
                'shards': [{'name': 'test_standalone_master0'}],
            },
        },
    }
    return {'SECDIST_CONFIG': json.dumps(secdist)}


@pytest.fixture
def extra_client_deps(redis_standalone_store):
    """Flush the standalone Redis between tests."""
