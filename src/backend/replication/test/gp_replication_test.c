#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "cmockery.h"

#include "postgres.h"
#include "storage/pg_shmem.h"

#define Assert(condition) if (!condition) AssertFailed()

bool is_assert_failed = false;

void AssertFailed()
{
	is_assert_failed = true;
}

/* Actual function body */
#include "../gp_replication.c"

static void
expect_lwlock(LWLockMode lockmode)
{
	expect_value(LWLockAcquire, lockid, SyncRepLock);
	expect_value(LWLockAcquire, mode, lockmode);
	will_be_called(LWLockAcquire);

	expect_value(LWLockRelease, lockid, SyncRepLock);
	will_be_called(LWLockRelease);
}

static void
expect_elog()
{
	expect_any(elog_start, filename);
	expect_any(elog_start, lineno);
	expect_any(elog_start, funcname);
	will_be_called(elog_start);

	expect_any(elog_finish, elevel);
	expect_any(elog_finish, fmt);
	will_be_called(elog_finish);
}

static void
expect_ereport()
{
	expect_any(errstart, elevel);
	expect_any(errstart, filename);
	expect_any(errstart, lineno);
	expect_any(errstart, funcname);
	expect_any(errstart, domain);

	will_be_called(errstart);
}

static void
test_setup(WalSndCtlData *data, WalSndState state)
{
	max_wal_senders = 1;
	WalSndCtl = data;
	data->walsnds[0].pid = 1;
	data->walsnds[0].state = state;

	expect_lwlock(LW_SHARED);
}

void
test_GetMirrorStatus_Pid_Zero(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	max_wal_senders = 1;
	WalSndCtl = &data;
	data.walsnds[0].pid = 0;
	/*
	 * This would make sure Mirror is reported as DOWN, as grace period
	 * duration is taken into account.
	 */
	data.walsnds[0].marked_pid_zero_at_time =
		((pg_time_t) time(NULL)) - FTS_MARKING_MIRROR_DOWN_GRACE_PERIOD;

	/*
	 * Ensure the recovery finished before wal sender died.
	 */
	PMAcceptingConnectionsStartTime = data.walsnds[0].marked_pid_zero_at_time - 1;

	expect_lwlock(LW_SHARED);
	GetMirrorStatus(&response);

	assert_false(response.RequestRetry);
	assert_false(response.IsMirrorUp);
	assert_false(response.IsInSync);
}

void
test_GetMirrorStatus_RequestRetry(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	max_wal_senders = 1;
	WalSndCtl = &data;
	data.walsnds[0].pid = 0;
	/*
	 * Make the pid zero time within the grace period.
	 */
	data.walsnds[0].marked_pid_zero_at_time =
		((pg_time_t) time(NULL)) - FTS_MARKING_MIRROR_DOWN_GRACE_PERIOD/2;

	/*
	 * Ensure recovery finished before wal sender died.
	 */
	PMAcceptingConnectionsStartTime = data.walsnds[0].marked_pid_zero_at_time - FTS_MARKING_MIRROR_DOWN_GRACE_PERIOD;

	expect_lwlock(LW_SHARED);
	expect_ereport();
	GetMirrorStatus(&response);

	assert_true(response.RequestRetry);
}

/*
 * Verify the logic the grace period will exclude the recovery time.
 */
void
test_GetMirrorStatus_Delayed_AcceptingConnectionsStartTime(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	max_wal_senders = 1;
	WalSndCtl = &data;
	data.walsnds[0].pid = 0;
	/*
	 * wal sender pid zero time over the grace period
	 * Mirror will be marked down, and no retry.
	 */
	data.walsnds[0].marked_pid_zero_at_time =
		((pg_time_t) time(NULL)) - FTS_MARKING_MIRROR_DOWN_GRACE_PERIOD;

	/*
	 * However the database was in recovery, hence
	 * we are still within the grace period, and
	 * we should retry.
	 */
	PMAcceptingConnectionsStartTime = ((pg_time_t) time(NULL)) - FTS_MARKING_MIRROR_DOWN_GRACE_PERIOD/2;

	expect_lwlock(LW_SHARED);
	expect_ereport();
	GetMirrorStatus(&response);

	assert_true(response.RequestRetry);
}

void
test_GetMirrorStatus_Overflow(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	max_wal_senders = 1;
	WalSndCtl = &data;
	data.walsnds[0].pid = 0;
	/*
	 * This would make sure Mirror is reported as DOWN, as grace period
	 * duration is taken into account.
	 */
	data.walsnds[0].marked_pid_zero_at_time = INT64_MAX;
	PMAcceptingConnectionsStartTime = ((pg_time_t) time(NULL));

	expect_lwlock(LW_SHARED);
	GetMirrorStatus(&response);

	assert_false(response.RequestRetry);
	assert_false(response.IsMirrorUp);
	assert_false(response.IsInSync);
}

void
test_GetMirrorStatus_WALSNDSTATE_STARTUP(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	test_setup(&data, WALSNDSTATE_STARTUP);
	GetMirrorStatus(&response);

	assert_false(response.IsMirrorUp);
	assert_false(response.IsInSync);
}

void
test_GetMirrorStatus_WALSNDSTATE_BACKUP(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	test_setup(&data, WALSNDSTATE_BACKUP);
	GetMirrorStatus(&response);

	assert_false(response.IsMirrorUp);
	assert_false(response.IsInSync);
}

void
test_GetMirrorStatus_WALSNDSTATE_CATCHUP(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	test_setup(&data, WALSNDSTATE_CATCHUP);
	GetMirrorStatus(&response);

	assert_true(response.IsMirrorUp);
	assert_false(response.IsInSync);
}

void
test_GetMirrorStatus_WALSNDSTATE_STREAMING(void **state)
{
	FtsResponse response;
	WalSndCtlData data;

	test_setup(&data, WALSNDSTATE_STREAMING);
	GetMirrorStatus(&response);

	assert_true(response.IsMirrorUp);
	assert_true(response.IsInSync);
}

int
main(int argc, char* argv[])
{
	cmockery_parse_arguments(argc, argv);

	const UnitTest tests[] = {
		unit_test(test_GetMirrorStatus_Pid_Zero),
		unit_test(test_GetMirrorStatus_RequestRetry),
		unit_test(test_GetMirrorStatus_Delayed_AcceptingConnectionsStartTime),
		unit_test(test_GetMirrorStatus_Overflow),
		unit_test(test_GetMirrorStatus_WALSNDSTATE_STARTUP),
		unit_test(test_GetMirrorStatus_WALSNDSTATE_BACKUP),
		unit_test(test_GetMirrorStatus_WALSNDSTATE_CATCHUP),
		unit_test(test_GetMirrorStatus_WALSNDSTATE_STREAMING)
	};
	return run_tests(tests);
}
