RegisterEvent('echo_req', function(_, _, eventData, playerIndex)
    MsSendMlEvent(playerIndex, 'echo_res', EXAMPLE_CLOUD_NAME .. ':' .. eventData)
end)

RegisterEvent('ordered_req', function(_, _, eventData, playerIndex)
    MsSendMlEvent(playerIndex, 'other_res', 'kept')
    MsSendMlEvent(playerIndex, 'ordered_res', eventData)
end)
