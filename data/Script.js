// Removed openPage and validate functions as the admin page/tab was removed.
// Removed Save_Other_Config function.

function displayConfirmation(message, targetId = 'Wifi_cred', isError = false) {
    // Uses CSS variable for color for consistency with dark mode
    const targetElement = document.getElementById(targetId);
    if (targetElement) {
        targetElement.innerHTML = `<div id="confirmation-message" style="color: ${isError ? 'red' : 'var(--primary-color)'};">${message}</div>`;
        targetElement.style.display = "block";
    }
}

async function wifi_list() {
    const result = await fetch("api/scanlist");
    const data = await result.json();
    var e1 = document.getElementById('ssidList');
    // Clear existing list and reset selection message
    e1.innerHTML = '<option value="" disabled selected>Select a network</option>';
    
    var obj = data;
    // Assuming the API returns an object with a 'ssid' array
    var value = obj.ssid;
    if (value && value.length > 0) {
        for (let i = 0; i < value.length; i++) {
            var newOption = document.createElement("option");
            newOption.value = value[i];
            newOption.innerHTML = value[i];
            e1.appendChild(newOption);
        }
    } else {
        e1.innerHTML = '<option value="" disabled selected>No networks found. Try Rescan.</option>';
    }
}

function Submit_Device_config(event) {
    // Prevent the default form submit behavior
    if (event) event.preventDefault();

    var sel = document.getElementById('ssidList');
    var ssidOption = getSelectedOption(sel);
    var ssidVal = ssidOption ? ssidOption.innerHTML : null;
    var ssidPasswordVal = document.getElementById("wifipassword").value;

    if (!ssidVal || !ssidPasswordVal) {
        alert("Please select a Wi-Fi network and enter a password.");
        return;
    }

    var JSONObj = { "ssid": ssidVal, "password": ssidPasswordVal };

    // Display confirmation message using modern DOM replacement
    displayConfirmation(`Connecting to Wi-Fi network: <strong>${ssidVal}</strong>.`);

    const newLocal = "/api/Wificred";
    fetch(newLocal, {
        method: "POST",
        body: JSON.stringify(JSONObj),
        headers: {
            "Content-type": "application/json; charset=UTF-8"
        }
    }).then(response => {
        if (response.ok) {
            alert("Successfully received credentials.");
        } else {
            alert("Error: invalid credentials or connection issue");
        }
    }).catch(error => {
        alert("Network error: Could not connect to the device.");
        console.error('Fetch error:', error);
    });
}

function getSelectedOption(sel) {
    var opt;
    for (var i = 0, len = sel.options.length; i < len; i++) {
        opt = sel.options[i];
        if (opt.selected === true) {
            break;
        }
    }
    return opt;
}