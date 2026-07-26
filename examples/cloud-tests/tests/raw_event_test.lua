TEST_CASE('raw event round trip', '[example][event]', function(ctx)
    local client = ctx:client(0)

    REQUIRE_EQ(client:send('echo_req', 'hello'), 0)
    local response = client:waitEvent({name = 'echo_res'})

    REQUIRE(response)
    CHECK_EQ(response.data, 'mls-sim-example:hello')
end)

TEST_CASE('waitEvent preserves unmatched events', '[example][event]', function(ctx)
    local client = ctx:client(0)

    REQUIRE_EQ(client:send('ordered_req', 'target'), 0)
    local response = client:waitEvent({name = 'ordered_res'})

    REQUIRE(response)
    CHECK_EQ(response.data, 'target')
    local remaining = client:poll()
    REQUIRE_EQ(#remaining, 1)
    CHECK_EQ(remaining[1].name, 'other_res')
end)

TEST_CASE('SECTION paths use isolated rooms', '[example][section]', function(ctx)
    local client = ctx:client(0)

    SECTION('first path', function()
        REQUIRE_EQ(client:send('echo_req', 'first'), 0)
        local response = client:waitEvent({name = 'echo_res'})
        REQUIRE(response)
        CHECK_EQ(response.data, 'mls-sim-example:first')
    end)

    SECTION('second path', function()
        REQUIRE_EQ(client:send('echo_req', 'second'), 0)
        local response = client:waitEvent({name = 'echo_res'})
        REQUIRE(response)
        CHECK_EQ(response.data, 'mls-sim-example:second')
    end)
end)

TEST_CASE('clearEvents removes preserved events', '[example][event]', function(ctx)
    local client = ctx:client(0)

    REQUIRE_EQ(client:send('ordered_req', 'target'), 0)
    REQUIRE(client:waitEvent({name = 'ordered_res'}))
    CHECK_EQ(client:clearEvents(), 1)
    CHECK_EQ(#client:poll(), 0)
end)

TEST_CASE('player lifecycle uses host commands', '[example][lifecycle]', function(ctx)
    local client = ctx:client(0)

    CHECK_EQ(ctx:playerLeave({index = 0}), 0)
    CHECK_EQ(ctx:playerJoin({index = 0, name = 'ExamplePlayer'}), 0)
    CHECK_EQ(ctx:playerExit({index = 0}), 0)
    CHECK_EQ(client:send('echo_req', 'after-exit'), 3)
end)
