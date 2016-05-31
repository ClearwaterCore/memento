Memento Overview
================

Memento is an IMS application server. It uses the ISC interface for processing SIP call traffic and is invoked by Sprout in the normal fashion using iFCs.
Memento also exposes an HTTP(S) interface to UEs to allow them to download the call list for their subscriber. This interface is similar to Ut.

SIP Interface
-------------

Memento acts as a SIP proxy. When it receives an initial INVITE it forwards the INVITE back to Sprout and record-routes itself into the dialog for the call.

Memento writes information about the call to persistent storage (supplied by a cassandra database) for later retrieval by the UE. These call list fragments are written when a call begins, when a call ends, and when a call is rejected.

The contents of a call list entry are derived from the SIP signalling, and an example call list fragment (for a rejected call is):

```
<to>
  <URI>alice@example.com</URI>
  <name>Alice Adams</name>
</to>
<from>
  <URI>bob@example.com</URI>
  <name>Bob Barker</name>
</from>
<answered>0</answered>
<outgoing>1</outgoing>
<start-time>2002-05-30T09:30:10</start-time>
```

Apart from INVITEs and BYEs, any other mid-call messages are simply forwarded according to their route header (for a request) or via headers (for a response). If Memento receives an initial request other than an INVITE, it simply forwards the request back to Sprout and does not record-route itself.

HTTP Interface
--------------

Memento exposes the following URL for retrieving call lists for a user:

    /org.projectclearwater.call-list/users/<IMPU>/call-list.xml

This supports GETs to retrieve an entire call list for a public user identity; all other methods return a 405.

Requests to this URL must be authenticated. Memento uses [HTTP Digest authentication] (http://tools.ietf.org/html/rfc2617), and supports the "auth" quality of protection. Memento uses the credentials provisioned in homestead for authenticating requests, in a similar way to how Sprout authenticates SIP REGISTERs. Memento also authorizes requests, ensuring that the authenticated IMPI is permitted to access the IMPU referred to in the URL of the request.

Alternatively, trusted servers can authenticate requests using an NGV-API-Key header containing the API key defined by the `memento_api_key` setting in shared config.  Requests authenticated with this mechanism are authorized to access any call list.

If the request has been authorized and authenticated Memento retrieves the call list fragments relating to the request IMPU from the call list store.
If there are entries, Memento will create an XML document containing complete calls and return this to the client

```
<call-list>
  <calls>
    <call>
      <to>
        <URI>alice@example.com</URI>
        <name>Alice Adams</name>
      </to>
      <from>
        <URI>bob@example.com</URI>
        <name>Bob Barker</name>
      </from>
      <answered>1</answered>
      <outgoing>1</outgoing>
      <start-time>2002-05-30T09:30:10</start-time>
      <answer-time>2002-05-30T09:30:20</answer-time>
      <answerer>
        <URI>bob@example.com</URI>
        <name>Bob Barker</name>
      </answerer>
      <end-time>2002-05-30T09:35:00</end-time>
    </call>
    <call>
      <to>
        <URI>alice@example.net</URI>
        <name>Alice Adams</name>
      </to>
      <from>
        <URI>bob@example.net</URI>
        <name>Bob Barker</name>
      </from>
      <answered>0</answered>
      <outgoing>1</outgoing>
      <start-time>2002-05-30T09:30:10</start-time>
    </call>
  </calls>
</call-list>
```

If there are no entries, Memento will respond with an empty call list, i.e.:

    <call-list><calls></calls></call-list>

Memento supports gzip compression of the call list document, and will compress it in the HTTP response if the requesting client indicates it is willing to accept gzip encoding.

HTTP Notification Interface
---------------------------

Mement supports notifications via HTTP whenever a subscriber's call list is updated.  When a notification URL is configured, memento will make a POST to that URL each time call list is updated.  The body of the POST request will be a JSON document of the form:

```json
{ "impu": "<subscriber's IMPU>" }
```

Configuration
-------------

Memento is configured for a subscriber in their IFCs, and is registered as a general AS for INVITE requests. The application server name should be set to `memento` @ the cluster of nodes running the Memento application servers (which will be the Sprout cluster, or a standalone application server cluster). 

An example IFC is:

```
<InitialFilterCriteria>
  <TriggerPoint>
  <ConditionTypeCNF>0</ConditionTypeCNF>
  <SPT>
    <ConditionNegated>0</ConditionNegated>
    <Group>0</Group>
    <Method>INVITE</Method>
    <Extension></Extension>
  </SPT>
  </TriggerPoint>
  <ApplicationServer><ServerName>sip:memento@memento.cw-ngv.com</ServerName>
    <DefaultHandling>0</DefaultHandling>
  </ApplicationServer>
</InitialFilterCriteria>
```

There are also five deployment wide configuration options. These should be set in /etc/clearwater/config:

* `max_call_list_length`: This determines the maximum number of complete calls a subscriber can have in the call list store. This defaults to no limit.
* `call_list_store_ttl`: This determines how long each call list fragment should be kept in the call list store. This defaults to 604800 seconds (1 week).
* `memento_threads`: This determines the number of threads dedicated to adding call list fragments to the call list store. This defaults to 25 threads
* `memento_disk_limit`: This determines the maximum size that the call lists database may occupy. This defaults to 20% of disk space.
* `memento_notify_url`: If set, memento will make a POST request to this URL whenever a subscriber's call list is changed.

Scalability
-----------

Memento is horizontally scalable. A cluster of Memento nodes provides access to the same
underlying Cassandra cluster and memcached cluster, allowing the load to be spread between nodes.
