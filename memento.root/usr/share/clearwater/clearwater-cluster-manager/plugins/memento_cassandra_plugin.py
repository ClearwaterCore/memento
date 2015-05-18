from metaswitch.clearwater.cluster_manager.plugin_base import \
    SynchroniserPluginBase
from metaswitch.clearwater.cluster_manager.plugin_utils import \
    join_cassandra_cluster, leave_cassandra_cluster
from metaswitch.clearwater.cluster_manager.alarms import issue_alarm
from metaswitch.clearwater.cluster_manager import constants
import logging

_log = logging.getLogger("memento_cassandra_plugin")


class MementoCassandraPlugin(SynchroniserPluginBase):
    def __init__(self, ip, local_site, remote_site):
        self._ip = ip
        self._local_site = local_site
        _log.debug("Raising Cassandra not-clustered alarm")
        issue_alarm(constants.RAISE_CASSANDRA_NOT_YET_CLUSTERED)

    def key(self):
        return "/memento/clustering/cassandra"

    def on_cluster_changing(self, cluster_view):
        pass

    def on_joining_cluster(self, cluster_view):
        join_cassandra_cluster(cluster_view,
                               "/etc/cassandra/cassandra.yaml",
                               "/etc/cassandra/cassandra-rackdc.properties",
                               self._ip,
                               self._local_site)

        if (self._ip == sorted(cluster_view.keys())[0]):
            _log.debug("Adding Memento schema")
            run_command("/usr/share/clearwater/cassandra-schemas/memento.sh")

        _log.debug("Clearing Cassandra not-clustered alarm")
        issue_alarm(constants.CLEAR_CASSANDRA_NOT_YET_CLUSTERED)

    def on_new_cluster_config_ready(self, cluster_view):
        pass

    def on_stable_cluster(self, cluster_view):
        pass

    def on_leaving_cluster(self, cluster_view):
        issue_alarm(constants.RAISE_CASSANDRA_NOT_YET_DECOMMISSIONED)
        leave_cassandra_cluster()
        issue_alarm(constants.CLEAR_CASSANDRA_NOT_YET_DECOMMISSIONED)
        pass

    def files(self):
        return ["/etc/cassandra/cassandra.yaml"]


def load_as_plugin(ip, local_site, remote_site):
    return MementoCassandraPlugin(ip, local_site, remote_site)
